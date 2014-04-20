/* The Grenade plugin creates a custom flag weapon - Grenade.
 *
 * Grenade shoots a forward PZ shot that detonates with a SW after a fixed delay.
 * The detonation distance is specified according to the forward tank speed at the time of shooting.
 * Going backwards or staying still yields the minimum grenade range; going full speed forward yields max range.
 * The PZ shot can travel vertically (if the server variable is set) and bounce off the ground. It also detonates
 * immediately on contact with a world wall.
 *
 * The server variables should be self-explanatory:
 *  _grenadeMinRange
 *  _grenadeMaxRange
 *  _grenadeTriggerTime
 *  _grenadeShockDuration
 *  _grenadeUseVerticalVelocity
 *
 * For maximum security these variables should be -setforced in command line options so that they don't disappear
 * when an admin uses "/reset *"
 */

#include "bzfsAPI.h"
#include "../src/bzfs/bzfs.h"

#include <memory>

using namespace std;

static int sendShot(FlagType *f, float lifetime, TeamColor t, float *p,
                    float tilt, float dir, int shotId, float speed) {
  void *buf, *bufStart = getDirectMessageBuffer();
  FiringInfo firingInfo;
  firingInfo.timeSent = (float)TimeKeeper::getCurrent().getSeconds();
  firingInfo.flagType = f;
  firingInfo.lifetime = lifetime;
  firingInfo.shot.player = ServerPlayer;
  memmove(firingInfo.shot.pos, p, 3 * sizeof(float));
  const float tiltFactor = cosf(tilt);
  firingInfo.shot.vel[0] = speed * tiltFactor * cosf(dir);
  firingInfo.shot.vel[1] = speed * tiltFactor * sinf(dir);
  firingInfo.shot.vel[2] = speed * sinf(tilt);
  firingInfo.shot.id = shotId;
  firingInfo.shot.dt = 0.0f;
  firingInfo.shot.team = t;
  buf = firingInfo.pack(bufStart);
  broadcastMessage(MsgShotBegin, (char *) buf - (char *) bufStart, bufStart);
  return shotId;
}

// This assumes the map doesn't have world GrenadePlugin in the .bzw
static int getShotID(int player) {
  static int i = 0;
  ++i;
  i &= 255;
  return 256 * player + i;
}

static int fireShot(const char *flag, float lifetime, PlayerId id, bz_eTeamType t, float *p,
                    float tilt, float dir, float speed) {
  if (!p || !flag)
    return -1;
  FlagTypeMap &flagMap = FlagType::getFlagMap();
  if (flagMap.find(std::string(flag)) == flagMap.end())
    return -1;
  FlagType *type = flagMap.find(std::string(flag))->second;

  return sendShot(type, lifetime, (TeamColor) convertTeam(t), p, tilt, dir, getShotID(id), speed);
}

static int fireShot(const char *flag, float lifetime, PlayerId id, bz_eTeamType t, float *p,
                    float *v) {
  float speed = sqrt(pow(v[0], 2) + pow(v[1], 2) + pow(v[2], 2));
  float tilt = asin(v[2] / speed);
  float dir = atan2(v[1], v[0]);           
  return fireShot(flag, lifetime, id, t, p, tilt, dir, speed);
}

static int getKiller(int shotID) {
  int killer = shotID / 256;
  if (!unique_ptr<bz_BasePlayerRecord>(bz_getPlayerByIndex(killer))) {
    return ServerPlayer;
  }
  return killer;
}

struct DelayedShot {
  const char *flag;
  float lifetime;
  bz_eTeamType team;
  float pos[3];
  float tilt;
  float dir;
  float speed;
  float delay;
  int owner;
  DelayedShot(const char *flag, float lifetime, bz_eTeamType team, float pos[3], float tilt, float dir, float speed, float delay, int owner) :
              flag(flag), lifetime(lifetime), team(team), tilt(tilt), dir(dir), speed(speed), delay(delay), owner(owner) {
    for (int i = 0; i < 3; i++) {
      this->pos[i] = pos[i];
    }
  }
};

static vector<DelayedShot> delayedShots;

static void fireDelayedShot(const char *flag, float lifetime, PlayerId id, bz_eTeamType t, float *p,
                    float tilt, float dir, float speed, float delay) {
  if (delay <= 0.0f) {
    fireShot(flag, lifetime, id, t, p, tilt, dir, speed);
  }
  DelayedShot s(flag, lifetime, t, p, tilt, dir, speed, delay, id);
  delayedShots.push_back(s);
}

static void fireDelayedShot(const char *flag, float lifetime, PlayerId id, bz_eTeamType t, float *p,
                    float *v, float delay) {
  float speed = sqrt(pow(v[0], 2) + pow(v[1], 2) + pow(v[2], 2));
  float tilt = asin(v[2] / speed);
  float dir = atan2(v[1], v[0]);
  fireDelayedShot(flag, lifetime, id, t, p, tilt, dir, speed, delay);
}

static void updateDelayedShotQueue() {
  static double prevTime = bz_getCurrentTime();
  
  double curTime = bz_getCurrentTime();
  double dt = max(0.0, curTime - prevTime);
  
  prevTime = curTime;
  
  for (auto it = delayedShots.begin(); it != delayedShots.end();) {
    it->delay -= dt;
    if (it->delay <= 0.0f) {
      fireShot(it->flag, it->lifetime, it->owner, it->team, it->pos, it->tilt, it->dir, it->speed);
      it = delayedShots.erase(it);
    }
    else {
      it++;
    }
  }
}

static void fireGrenade(bz_ShotFiredEventData_V1 *d) {
  unique_ptr<bz_BasePlayerRecord> b(bz_getPlayerByIndex(d->playerID));
  
  float speed = sqrtf(powf(b->lastKnownState.velocity[0], 2.0f) + powf(b->lastKnownState.velocity[1], 2.0f));
  const float veldir = atan2f(b->lastKnownState.velocity[1], b->lastKnownState.velocity[0]);
  float diff = b->lastKnownState.rotation - veldir;
  diff = atan2f(sinf(diff), cosf(diff));
  if (fabs(diff) > M_PI / 2.0f)
    speed *= -1.0f;
  if (speed < 0.0f)
    speed = 0.0f;
    
  float minRange = (float) bz_getBZDBDouble("_grenadeMinRange");
  float maxRange = (float) bz_getBZDBDouble("_grenadeMaxRange");
  float lifetime = (float) bz_getBZDBDouble("_grenadeTriggerTime");
  float duration = (float) bz_getBZDBDouble("_grenadeShockDuration");
  float range = minRange + (maxRange - minRange) * speed / bz_getBZDBDouble("_tankSpeed");
  
  float sp = sqrtf(powf(d->vel[0], 2.0f) + powf(d->vel[1], 2.0f));
  
  float shotVel[3];
  shotVel[0] = d->vel[0] / sp * range / lifetime;
  shotVel[1] = d->vel[1] / sp * range / lifetime;
  shotVel[2] = bz_getBZDBBool("_grenadeUseVerticalVelocity") * b->lastKnownState.velocity[2];

  // Detonate at position in future.
  // If grenade would hit a world wall before its expiry, detonate at the wall instead.
  float collisionTime = lifetime;
  float wallPos = (float) bz_getBZDBDouble("_worldSize") / 2.0f;
  float collisionTimeTemp;
  
  collisionTimeTemp = (wallPos - d->pos[0]) / shotVel[0];
  if (collisionTimeTemp > 0.0f && collisionTimeTemp < collisionTime)
    collisionTime = collisionTimeTemp;
  
  collisionTimeTemp = (-wallPos - d->pos[0]) / shotVel[0];
  if (collisionTimeTemp > 0.0f && collisionTimeTemp < collisionTime)
    collisionTime = collisionTimeTemp;
  
  collisionTimeTemp = (wallPos - d->pos[1]) / shotVel[1];
  if (collisionTimeTemp > 0.0f && collisionTimeTemp < collisionTime)
    collisionTime = collisionTimeTemp;
  
  collisionTimeTemp = (-wallPos - d->pos[1]) / shotVel[1];
  if (collisionTimeTemp > 0.0f && collisionTimeTemp < collisionTime)
    collisionTime = collisionTimeTemp;
  
  // This shot will end on a client if it hits the ground
  fireShot("PZ", collisionTime, d->playerID, bz_getPlayerTeam(d->playerID), d->pos, shotVel);
  
  float explodePos[3];
  float groundCollisionTime = -d->pos[2] / shotVel[2];
  if (groundCollisionTime > 0.0f && groundCollisionTime < collisionTime) {
    float groundpos[3];
    groundpos[0] = d->pos[0] + shotVel[0] * groundCollisionTime;
    groundpos[1] = d->pos[1] + shotVel[1] * groundCollisionTime;
    groundpos[2] = d->pos[2] + shotVel[2] * groundCollisionTime;
    float bouncevel[3];
    bouncevel[0] = shotVel[0];
    bouncevel[1] = shotVel[1];
    bouncevel[2] = -shotVel[2];
    fireDelayedShot("PZ", collisionTime - groundCollisionTime, d->playerID, bz_getPlayerTeam(d->playerID), groundpos, bouncevel, groundCollisionTime);
    
    explodePos[0] = d->pos[0] + shotVel[0] * collisionTime;
    explodePos[1] = d->pos[1] + shotVel[1] * collisionTime;
    explodePos[2] = groundpos[2] + bouncevel[2] * (collisionTime - groundCollisionTime);
    fireDelayedShot("SW", duration, d->playerID, bz_getPlayerTeam(d->playerID), explodePos, 0.0f, 0.0f, bz_getBZDBDouble("_shotSpeed"), collisionTime);
  }
  else {
    explodePos[0] = d->pos[0] + shotVel[0] * collisionTime;
    explodePos[1] = d->pos[1] + shotVel[1] * collisionTime;
    explodePos[2] = d->pos[2] + shotVel[2] * collisionTime;
    fireDelayedShot("SW", duration / bz_getBZDBDouble("_shockAdLife"), d->playerID, bz_getPlayerTeam(d->playerID), explodePos, 0.0f, 0.0f, bz_getBZDBDouble("_shotSpeed"), collisionTime);
  }
}

// FIXME: should remove shots if their owners leave

class GrenadePlugin : public bz_Plugin {
public:
  virtual const char* Name () {return "Grenade";}
  virtual void Init (const char* config);
  virtual void Event (bz_EventData *eventData);
  virtual void Cleanup (void);
};

BZ_PLUGIN(GrenadePlugin)

void GrenadePlugin::Init (const char* commandLine) {
  Register(bz_ePlayerDieEvent);
  Register(bz_eShotFiredEvent);
  Register(bz_eTickEvent);

  if (!bz_BZDBItemExists("_grenadeMinRange"))
  {
    bz_setBZDBDouble("_grenadeMinRange", 65.0);
  }
  if (!bz_BZDBItemExists("_grenadeMaxRange"))
  {
    bz_setBZDBDouble("_grenadeMaxRange", 300.0);
  }
  if (!bz_BZDBItemExists("_grenadeTriggerTime"))
  {
    bz_setBZDBDouble("_grenadeTriggerTime", 1.0);
  }
  if (!bz_BZDBItemExists("_grenadeShockDuration"))
  {
    bz_setBZDBDouble("_grenadeShockDuration", 2.0);
  }
  if (!bz_BZDBItemExists("_grenadeUseVerticalVelocity"))
  {
      bz_setBZDBBool("_grenadeUseVerticalVelocity", true);
  }
  
  bz_RegisterCustomFlag("GN", "Grenade", "Forward tank speed determines grenade range. Grenade can travel vertically, bounce off ground and detonate against world walls.", 0, eGoodFlag);

  MaxWaitTime = 0.001f;
}

void GrenadePlugin::Cleanup (void) {
  Flush();
}

void GrenadePlugin::Event (bz_EventData *eventData) {
  updateDelayedShotQueue();

  if (eventData->eventType == bz_ePlayerDieEvent) {
    bz_PlayerDieEventData_V1* dieData = (bz_PlayerDieEventData_V1*)eventData;
    if (dieData->flagKilledWith == "SW" && dieData->killerID == 253) { // Assume no other custom flag uses SW!
      dieData->killerID = getKiller(dieData->shotID);
    }
  }
  else if (eventData->eventType == bz_eShotFiredEvent) {
    bz_ShotFiredEventData_V1* shotFiredData = (bz_ShotFiredEventData_V1*)eventData;
    if (shotFiredData->type == "GN")
      fireGrenade(shotFiredData);
  }
}
