## Outstanding Bugs

Originally created by me (i.e. technically author of this repo, not Claude) to report bugs I found during playtesting; will be updated over time to reflect their status and may be updated with other bugs me or Claude find.

* Portal Ed Clone: after disappearing through a portal, a "clone" Ed is left behind. This may be a regression as it didn't happen before the previous update; possibly something to do with how "simple Ed" is still tied up in the physics engine. Fixing may uncover other pain points.

* Dodgy Edge Collision: jumping at some platforms causes Ed to repel off them and fall through, even though he should have grabbed on. May also have to do with his angle of approach. A few other related things feel off: the angle at which he grabs not being normalized to the edge itself can make him end up in weird angles which treat movement strangely.

* Ghost Water Bodies: namely under Doc's house, in the entrance to Pressure Cooker, but may also be present in other areas. Possibly level-export related.

* Water Jump Buoyancy: when Ed jumps into the water, he sinks way too low. This sometimes triggers his swimming movement too early.

## Bigger Areas of Improvement

These might not necessarily be called bugs, but rather more overarching issues with certain parts of the game. Also not including obvious ones, like implementing actual game mechanics, story progression, item collection etc. There's a lot to go still.

* Level Export Amendments: noticed some teleports not walking in sud0; walking into the cave that connects Doc's House, walking into his house... There are also some moving / disappearing textures that are rendered and with collision, we can take these one by one; the first one is in sud0 right outside the Ski Slope portal to the left. Before rescuing Doc in the actual game the area is blocked off; it opens up afterwards. Would be good to keep it open for now as we're quite a ways off with implementing full game flow and mechanics.

* Integrating Cutscenes: this would be a fun bit of work that would involve some custom spawn-locations (we'll eventually need to add Tooling!! see next item)

* Tonic Tooling: we're building a game engine here. So a full editing suite for it would enable freedom in doing things such as: making the lake at the bottom of the Vegetable HQ teleport location swimmable (and adding some way to get back up, too!) In the original game you die by falling there, but it'd be cool to actually swim in it and climb back up. Many other such situations, but also giving way to creating whole new levels. I still want to see Tonic Trouble 2, and am also envisioning my own interactive action-adventure ideas in this engine. This admittedly big piece of work could make or break the project, so be very careful. But still...