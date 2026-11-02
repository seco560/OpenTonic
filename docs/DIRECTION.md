Yes, we're just getting started. But I think it's a better can of worms than it looks, and a separate `gameplay-systems` branch is the right call: this will be unstable for a while and `main` should stay playable.

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