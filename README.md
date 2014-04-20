Grenade
=======

Grenade custom flag plugin for BZFlag 2.4

Grenade shoots a forward PZ shot that detonates with a SW after a fixed delay.
The detonation distance is specified according to the forward tank speed at the time of shooting.
Going backwards or staying still yields the minimum grenade range; going full speed forward yields max range.
The PZ shot can travel vertically (if the server variable is set) and bounce off the ground. It also detonates
immediately on contact with a world wall.

Server Variables:
+ _grenadeMinRange
+ _grenadeMaxRange
+ _grenadeTriggerTime
+ _grenadeShockDuration
+ _grenadeUseVerticalVelocity
