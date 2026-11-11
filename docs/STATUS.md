# Status: one row per system

Written 2026-11-11. Retail only (the Special Edition is parked). For how the parts fit together: docs/OVERVIEW.md.

**How to read it.**
- **Built:** the code is there and the headless checks run it.
- **Seen:** what the user has said about it in the app. The source is the playtest interview in docs/BUGS.md
  (2026-11-11) and later remarks. "Not known" means nobody has said: it is not a claim that it works or that it fails.
- Where another document still says "awaiting playtest", this file and the interview win.

The gate, `make -C core checkall`, passed on 2026-11-11 on the levels in files/out (all thirteen checks, and the Special
Edition's sectors).

## Seen and working

| system | seen | known problems |
|---|---|---|
| Levels drawn (geometry, textures, lighting, fog) | yes; the user parked rendering as "good enough" | Agent XYZ's poster flickers in one small corner. Parked polish: bright corners, motion blur on slides |
| Sectors: what is drawn, whose water, per-sector fog and light | yes, fine | a ledge that ends at a portal can leave Ed in the old sector (Land, the pit at (163, 26)) |
| Ed on foot and in the air (his own scripts and physics) | yes | the ledge hang, below |
| Portals and level changes | yes; no Ed clone any more | the walk-in passages in sud0 play the portals' transition, and should not |
| The follow camera | yes: "fine, faithful down to the original's quirks" | it showed the insides of characters (Suzy, the tomatoes, Grogh's henchmen). Changed 2026-11-11; the user has not said whether it helped. Headless it is better, not gone (40 -> 10 frames on the worst probe) |
| Lifts and platforms | most work | one sometimes disappears mid-path; Ed flying goes through moving platforms |
| Levers | most work | the lever and machine turn to face Ed; Ed should take his place at them |
| Music, sounds of Ed, the intro, the menus | yes | some music tracks may be the wrong ones for their level (user, 2026-11-11): to check against the exe; not a priority |
| Recordings and replays | yes: "decent" | a replay being locked and one file per run were changed after that; not known if seen |
| The original game's demos | yes | on the ski demos the camera sits too far behind Ed; why is open |
| Levels imported from glTF, the portals between retail and Special south plains | yes (the user's own) | not test subjects |

## Seen and rough

| system | what the user saw |
|---|---|
| Water | jumping in is mostly fine. Ed clips through the bottom of water bodies. The underwater camera sits too high and is hard to steer |
| Ledge hang | Ed hangs at odd angles, or clips instead of grabbing |
| Pickups | some thermometers do not work (sud0, by the portal to the Special Edition's sud0). The antidotes play the wrong sound |
| Machines and puzzles | missing: the platforms on sud0's way to the north plain, pushing the boulders there, cavedoc's popcorn machine. 2026-11-11 later, headless: sud0's platforms move and cavedoc's crate snaps into place (docs/BUGS.md "cavedoc playtest"); not seen |
| Dying | in some pits Ed hits the bottom; some pits do nothing; after a respawn he is shown falling for half a second; lava and deadly water do not kill |
| Pushed things | cavedoc's crate moves but does not start the machine (fixed headless 2026-11-11 later: its snap marker's reference to it was dead; docs/BUGS.md "cavedoc playtest"). marmite's boxes fly off one way without colliding |
| Enemies | they can be killed and they hurt Ed; no proper sounds or collision |
| Shooting | aiming in first person does not tilt the blowpipe. "No shot leaves while one is in the air" (what the exe does there is not read) |
| Stick attacks | they hit, with no sound and no other feedback |
| Sounds of the cast | many unheard. A laugh loops in cavedoc (near the popcorn puzzle) |
| Hint scenes and speech | only bar is reliable. spynorth sometimes. pharmmap, pyr_fou and mammyon skip to the level without the line. SpyCarota can leave Ed stuck |
| The ski camera | "still odd" |

## Built, and not known if seen

| system | state |
|---|---|
| Ski physics against walls (second pass, 2026-10-31) | checked headless (skicheck); no remark from the user since |
| How the cast moves (2026-11-06: walking to points, turning, lobbed and homing things) | headless: more characters move and turn than before (movecheck). Which of it the user has seen is not known |
| Powers as real state (2026-11-11) | they start all on, so nothing should look different, and the user's test said "basically nothing has changed". A game with a power missing has never been seen in the app |
| The debug menu (2026-11-11, F4, keyboard only; docs/ENGINE.md "The debug menu") | built the same day, checked headless (debugcheck: power 7 off, reload, the river wall then stands). Never seen in the app; needs a reinstall (it also makes the survey the menu shows). The SDL build's part is type-checked only |
| Powers, lives and save values kept through a portal | checked headless (replaycheck) |
| Moving sectors, sud0's river wall (2026-11-11) | with all powers on Suzy drops the wall at the level's start, so the way is open as before; the user reported nothing wrong. The wall standing has never been seen. Not done: the characters inside a moved sector do not move with it; the turning sense is not verified |

## Stand-ins and gaps

These are not bugs: they are parts of the game that are faked or missing.

| what | today |
|---|---|
| Story progress | powers all on; removals under a story test are turned into frozen props at export; four of sud0's five gates are left out at export |
| Inventory | a stub: Ed has one of everything |
| Saving | the scripts' SaveGame is skipped |
| What the powers 7, 8, 0x70, 0x100 and 0x200 are | not read (0x80 is flying) |
| HUD: counters, boss bars, strings | not built |
| Lip sync | read, not built |
| Card types 8 and 9 (some props' physics) | they only turn in place |
| The camera's "watch him fall" mode; fixed shots at a ski finish | not ported |
| Shadows | parked; a stand-in blob |
| Hotspots, the other light kind, surface waves, sud0's snow | not ported |
| Portals, passages, Totalski's obstacles, light managers | data made by the converter, not characters running their scripts |

## What is next

1. The debug menu's first look in the app (built 2026-11-11). Then its second step: the settings now spread over the
   camera keys move into it, and the close-up key is remapped there.
2. Then sud0's other four gates, one at a time, and a "story start" with no powers.
3. The queue of rough things above, in the order the user picks.
