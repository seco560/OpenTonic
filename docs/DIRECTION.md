## Current plan: two games first, linked at the end (user, 2026-11-09)

The working plan. The Definitive Edition below stays the possible far goal, not the next step.

- **Implement Retail and Special separately and faithfully.** The player chooses which to play first.
- **The only link:** beating either game unlocks the other, through the portals in South Plain (tools/ttre/custom.py; today
  they are always open, with no condition). There are no other links between the two games.
- **Afterwards,** once that is settled: use the custom layer to connect other things.
- **Eventually, maybe:** both games merged and interleaved, through Tonic Tooling and the rest.

What follows from it (Claude's reading, not the user's words):
- Each edition keeps its own progress, save data, lives and menu. Only "this game was beaten" is shared. The two lives
  systems work completely differently (the user's pain point) and need no reconciling while the games are separate.
- Retail is still without real progression: game state incoherent, story gates removed at export, SaveGame skipped.
- The survey of what scripts read and write (save values, flags, story branches) is still useful, one edition at a time; whether
  the editions' values mean the same no longer matters yet. Not started.

**Two Eds (user, 2026-11-09).** The Special Edition's Ed moves completely differently (tank controls), so he will have to be
decompiled and reimplemented as well; today SE levels run on a borrowed retail Ed (docs/NOTES.md "Real debt"). Either Ed can
be chosen: at will while in development, later behind a flag. By default, going through a portal changes to that edition's
own Ed.

Also pending: the notes need a clean-up (NOTES.md is long and contradicts itself). The user wants Claude to do it, asking
them questions about what is in there.

---

## Future direction: Open Tonic, the Definitive Edition (user, 2026-11-05, later that day)

The user's design decision, written down as future direction; nothing below is built yet, and it is a long road.

- **Open Tonic is, by design, one merged game:** a Definitive Edition that combines retail Tonic Trouble and the
  Special Edition into a single, longer, more complete game. Ed moves between the levels of both and has to collect
  things from both.
- **The UI changes accordingly.** The main menu shows the pickups from both versions of the levels (each level's
  retail and SE collectibles side by side).
- **No faithful versions of the original games are maintained as products.** Bringing just one game's data to play
  that game ("what you bring decides what you play", below) is fine for now, while exploring levels, but it is not
  the goal any more.
- **Separation later, maybe:** options to play the editions apart get explored once the systems that carry all this
  are mapped out.

What this means for the work, as read so far (not the user's words): the exe is still read closely, because the
merged game is built from what both games actually do; "port choice" notes stay explicit, but the measure of a choice
becomes the merged game rather than fidelity to one edition. Shared state that now lives per level (World's counters,
the inventory, Ed's hit points and their maximum, save points) will have to become one game state across both
editions' levels: the pickups work of 2026-11-05 (docs/SE.md "Pickups") is the first piece that touches it.

## Earlier goal: two editions, one game (user, 2026-11-05)

Superseded as the end goal by the Definitive Edition above; kept for the history. A plan, not started; a long way
off. Written down as the user put it.

**What you bring decides what you play.**
- Bring the **Retail Edition** (its GAMEDATA): you play a version of retail Tonic Trouble.
- Bring the **Special Edition** (its disc image): you play a version of the Special Edition.
- Bring **both**: you play a combined game that takes you through both. The two versions of each level are
  interwoven like alternate universes, and certain portals move you between the retail and the Special Edition
  version of a place.

**Where it stands (2026-11-05).** `tools/convert_files.sh` already detects which game(s) `files/sources` holds and
converts each. The Special Edition's 11 levels run in the same engine as retail's, but only as a demo: they borrow the
retail Ed, and their own scripts and mechanics don't run yet (docs/SE.md). Both editions use the same 11 level names
(carota, cavedoc, cock01, land, lecanyon, marmite, north, outro, pyramide, sud0, totalski), which suggests natural
pairs for the crossings.

**What it needs.** A lot of formalising first:
- Both editions' game code on one footing: the SE's AI primitives mapped (its exe still has their names), its
  mechanics, its characters running their own scripts.
- A way to say where the crossings are, what a cross-edition portal does, which Ed (and which rules) apply on each
  side, and what carries over: progress, items, hit points.
- Authoring: this most likely needs **Tonic Tooling** (docs/TOOLING.md), with the core reachable from **Godot
  through GDExtension hooks**, so the combined game can be laid out and edited there. Until then, these notes are the
  plan.

---

## Initial Direction Change Trigger

// Began as an aside chat while implementing initial gameplay scripts.

...Yes, we're just getting started. But I think this is a better can of worms than it looks, and a separate `gameplay-systems` branch was the right call: this will be unstable for a while and `main` should stay playable.

The good news from today: the engine already has the abstraction, and we've been reading it piece by piece. Every character is a perso with the same parts:
- **std game:** active, dead, activation radius, hit points, run-mechanics and play-animation flags.
- **brain:** its scripts and variables.
- **3D data:** states, animations, transitions.
- **dynamics:** physics by ID card type.
- **collide sets and zones:** what it touches.
- **sector info:** where it is.

One loop runs them all every frame (`GAM_00428070`), in a known order. Cutscene waits, activations, pickups and doors aren't separate systems, just scripts using those parts. That's why the lever and the slab could reach into Ed today: in the original, Ed is just another perso.

So the abstraction I'd aim for is not a new design but the engine's own model, made explicit:

1. **A world of persos with those components, and one tick loop in the exe's order.** `tc_cast` is a first, partial version of it.
2. **Ed becomes a perso of that world, not a special case.** Today he has his own driver (`tc_ed_ai`), and the exporter boils other characters' effects on him down to data (portal op lists, ski triggers, the light characters). Each of those was useful scaffolding, and each one becomes something to retire once the general runtime covers it. Today's HEG_TransMaison duplication is the first sign of that pressure.
3. **Subsystems added one at a time, each read from the exe with a headless check**, as we did with collision and sectors. The next ones are zones (touch, hit, collect nearby), mechanics for the other card types (moving enemies and platforms), spawning (projectiles), then camera directors and dialogue (cutscenes).
4. **Faithful core, Remastered on top.** The "port choice" notes (turning in place, the hearing radius, starting counters at 0) should stay explicit and listed, so we always know which behaviour is the exe's and which is ours.

On Tonic Tooling: this model is also the answer there. If the editable source format describes levels as persos with these same components, plus scripts in a text form that compiles to the same nodes, then the editor, the game and the original data all share one vocabulary. Building the world model properly now is what makes tooling possible later. Building tooling first would mean designing it against a model we don't fully know yet.

Honestly, the scale is large: realistically many sessions, and parts of the exe nobody has read yet. But the biggest open risk was whether the script interpreter generalises beyond Ed. The measurements so far point to yes: over 85% primitive coverage and pickups working in headless runs. You haven't seen any of it in the game yet, so treat that as promising, not proven.
