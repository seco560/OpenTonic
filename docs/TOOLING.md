# Tonic Tooling: design considerations (later; agreed in discussion 2026-10-31 / 11-01)

The goal (user, BUGS.md "Tonic Tooling"): an editing suite for the engine, so that the user and anyone else can build
new worlds and games on it. For example: making the lake under the Vegetable HQ teleport swimmable, new levels, or a
Tonic Trouble 2. The user's warning: this could make or break the project, so be very careful. Nothing here is built
yet. These are the considerations reached so far, to start from when the work begins.

## Source -> cook -> runtime
- **`.ttlv` / `.ttai` stay the engine's cooked runtime formats.** Reasons from the code and docs (the original
  deliberation was not written down):
  - The original files are memory snapshots (encrypted blocks plus relocation tables), not designed formats.
  - The core has no file or OS code and loads from a memory buffer, so it needs a simple, flat format.
  - Baking happens once, offline (animation samples, radiosity bytes, PNG textures, world-space collision), which
    keeps the Vita and low-end PCs cheap.
  - No game data goes into the repo.
  - The converter's code stamp re-cooks the levels automatically after a converter change.
- **The editor does not edit `.ttlv` directly.** Baking loses what an editor needs:
  - one mesh per material for the whole level, so objects survive only as sector groups;
  - animations stored as sampled matrices, with no keyframes or curves;
  - collision as a flat triangle list;
  - the `.ttai`'s per-triangle tables, tied to that list by a CRC-32 of the COLL chunk (the LINK chunk).
- **Instead: an editable source format plus a cook step** that writes `.ttlv` / `.ttai`, the way modern engines turn
  source assets into runtime data. The converter (`tools/ttre`) already has two inputs: the original game data and
  `import_gltf.py`. A source-format input would be the third.
- **The core keeps loading cooked files only.** No JSON parsing or PNG decoding in the C core (the Vita budget).

## The source format
- **Assets: glTF 2.0** covers meshes, materials, textures, node hierarchies, keyframe animation and cameras.
  Blender and most other tools read and write it, and we already import it.
- **Engine data: a JSON level file** that points at the glTF assets. It holds what glTF has no standard place for:
  - sectors, portals and visibility lists;
  - collision and floor materials (grip, rebound);
  - water surfaces and their environments, fog per environment;
  - the original's lights: spherical / parallel / hotspot / ambient, near and far ranges, negative "darkeners";
  - baked light sets (day / night), material rules (alpha test, additive, wrapping), flipbook textures;
  - character placements and their scripts, triggers, level portals.
- **Why a separate file, not a glTF extension or `extras`:** both would work, but a split keeps the glTF clean for
  any tool and makes the engine data diff well in git. Revisit if round-tripping through Blender proves easier with
  everything in one file.
- **Collision tables are regenerated on every cook.** Any geometry edit invalidates the `.ttai`'s per-triangle tables,
  so the cook must rebuild them from the source, never carry stale ones over.

## Scripts
- **The `.ttai` is behaviour, not geometry**: script trees (behaviours, reflexes, conditions), dsgvars, states and
  transitions, buttons, portals and triggers. glTF cannot express it.
- **Scripts need a readable text or JSON form** and a compiler back to `.ttai`:
  - `level-ai` already prints Ed's behaviours as indented trees;
  - primitive names come from `tools/exe/ai_names.tsv`;
  - prior art: raytools/OpenSpaceCodeGen, which turns these scripts into code (check its licence before reusing).
- **The visual parts of the `.ttai`** (Ed's collision spheres and zones, trigger and portal volumes) can go into
  glTF as debug geometry for the editor to show.

## Bringing existing levels in
- **First step: a `.ttlv` -> glTF (+ level JSON) exporter**, so the original levels open in Blender:
  - geometry and textures as they are;
  - baked light as COLOR_0 (more sets as custom attributes, e.g. `_COLOR_1`);
  - sectors as node groups, their engine data in `extras` or the level file;
  - collision as a separate hidden mesh;
  - characters as animated nodes.
- **Part visibility:** some characters switch which part they show from frame to frame. Core glTF cannot animate
  visibility: use a visibility extension (KHR_node_visibility, check its status) or scale the part to zero.
- **Round-trip test as the safeguard:** an original level exported to source and cooked again should give the same
  `.ttlv` / `.ttai`, at least semantically.

## Constraints that stay
- **Clean room and legal:** the editor ships no game data; users convert their own copy, and new games can use fully
  original assets.
- **Faithful mechanics stay the engine's base** (docs/DECOMP.md); tooling adds content, it doesn't change the rules
  under the original levels.
- **The Tonic Viewer's all-sectors view stays** (docs/NOTES.md "Project direction"). Sector culling remains a choice
  made per mode, never baked into data.

## Open questions
- **Where the editor lives:** a Blender add-on, its own app built on the core and a frontend, or both (Blender for
  assets, our app for the engine data, play-testing and scripts).
- **The scripting language for new games:** keep the original's AI model (behaviours, reflexes, dsgvars) as the
  engine's language, or add something friendlier that compiles to it.
- **Authoring sectors:** by hand, or derived from portals the author places, and how to edit visibility lists.
- **Lighting new levels:** we have no radiosity baker; Blender can bake vertex colours, and lightmaps are another
  option.
