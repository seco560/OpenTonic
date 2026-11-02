1. Most gameplay triggers are distance checks, not collision. (read) Pickups have no collision at all. Their scripts sleep behind the activation radius (PROC 18/20), then check distance again with COND 61, which is really "Ed within r" (it was mislabelled "SpeedAbove?") and is used 143 times on sud0 alone. Distances are origin to origin in 3D. So how "grabbable" a pickup feels is the radius in its script, and the editor could expose that one number per pickup.

2. The whole story state is tiny. (read) FUNC 93 and PROC 143/144 read and write 31 shorts at DAT_004ec6d0. The main actor's hit points live in the same table, and the lives byte sits next to it. That looks like the game's entire progress record: story flags, counters such as bees, HP. (inference) A save system and story progression could mostly be "persist these 31 values plus lives". The port starts them at 0 because their starting values come from save data, so anything gated on story progress currently sees "start of the game".

3. Every interaction uses one pattern. (read) The prop writes itself into Ed's dsg57, maybe puts a parameter in another variable (the machine puts its Super Ed duration in dsg15), and forces one of Ed's behaviours. Ed then walks to a point given in the prop's own axes (OP_24(dsg57, LocalToGlobalPoint(offset))) and turns to face it. Levers, popcorn machines and carried objects all do this. Facing is a two-way zone test: the prop's zone must contain Ed, and Ed's zone 1 (a radius-3 sphere 3 units in front of him) must contain the prop. For your walkable cutscenes, "talk to the character to start the scene" can reuse this same protocol rather than a new mechanism.

4. Platforms that react to Ed standing on them are blocked by one gap. (read) FUNC 54 returns "the perso I'm standing on", and the sud0 slab (dalfou) uses it to rise. The port bakes every character's collision into the static world, so it can't know who owns the floor under Ed. Collision owned per character is the next mechanics piece for moving platforms, and it would also let the cast move.

5. A trap in how the scripts compare values. (read) "Me" is perso 0, so any unimplemented function that returned 0 compared equal to Me. That's why the slab grabbed Ed at the start of sud0. Unknowns now return NaN, which fails every comparison. Ed's checks came out identical, so I don't think it was silently affecting him before, but it was a real class of bug.

6. Small Super Ed details. (read)
- What grows during the transformation is Ed's shadow (Float13/14 are the shadow's scale), not his body.
- His model lives in Ed's own 306-object list. (inference) That list may hold other forms or props worth surveying, but I haven't checked which of the 306 objects no known state shows.
- In the headless runs Super Ed covered 7.4 units in 2 s against 10.5 for normal Ed. I haven't checked whether that matches the game, so it's worth watching in your playtest.

7. A chance to settle the 0.25 animation-speed mystery. The cast now plays on the exe's own animation clock, not the hand-matched 0.25 speed the baked loops use. If characters look about 4× too fast to you, the factor really is somewhere in the data. If they look right, the 0.25 was a quirk of how the baked loops were sampled.

8. Why the levers don't open anything yet. (read) The lever's own state stays at 0 even though Ed's grab, lean and release all work. Its scripts don't change its state themselves, so something outside them must drive its animation. (inference) Most likely Ed's pull states drive the lever, through an animation event or a channel link; that's the first thing I'd read next time.

## Second Pass

1. Stubs cause more bugs than missing features. Three of today's bugs came from stubs: a function returning 0 (which also means "Me"), ZoneTest always true, and private copies of other characters' variables. A missing primitive at least shows up in the skipped counts; a stub that returns a plausible constant fails silently. My suggestion is a stub registry. ai_names.tsv would mark each primitive as read and ported, stubbed (with its constant and why), or port choice. A small script would then compare that file with the case labels in tc_ai.c and fail if they disagree. That's cheap, and it's the formalization that would have caught today's slab bug before it happened.

2. The special cases should fold into the cast. Portals (PORT), Totalski's obstacles (TRIG) and the light characters (LAIS) were each a script pattern reduced to data, because the port couldn't run characters. Now it can. Over time each could go back to being an ordinary cast member running its own script, with portalcheck and crashcheck as the guard. Today I had to exclude them so they don't run twice, which means more port code to keep in sync. I'd do it one subsystem at a time, starting with the light characters as the smallest, and only once the cast has had some play.

3. The allow-list is a stopgap. MAY_FORCE_MAIN keeps you from getting stuck, but it says "the port can't finish what this character starts", not anything true about the game. Once cutscenes and dialogue run, the list should flip: everything allowed, with a short list of known-unfinished behaviours. Until then it will hide real interactions. Because it's a list, it's easy to see what's still missing.

4. **START WITH THIS** My headless scenarios are fragile. I had to fix castcheck several times today: teleports dropped Ed into holes, Super Ed timing, the level exit at the end of Transit's flig were the test, not the engine. Your playtests arethe real oracle, and right now they leave nothing reusable behind. If the frontends could record your input per tick, and the core could replay a recording headless, every playtest would become a regression test that follows real routes. I think that's the single best investment for "not having bugs forever

5. The docs need splitting. NOTES.md is about 1,750 lines of dated entries, and the README already warns it contradicts itself.
For a formal engine design I'd keep NOTES as the lo subsystem: AI value model, cast, mechanics, sectors, rendering, camera. Each would say what was read from the exe, what's ported, which choices are the port's own, and what's open, and would be rewritten rather than appended to. Your new DIRECTION.md looks like the right top layer for that.

6. What I haven't verified.
- Speed: the cast's cost per tick on the Vita. On the M1 it's negligible (castcheck simulates all 21 levels in seconds), and the activation radius keeps most characters asleep. BHz on a Vita is untested, and worth a quickmeasurement before the Vita work.
- Never seen: whether cast sounds are actually audible, what Super Ed looks like, and whether the character animations feel right in the app.