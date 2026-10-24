## Open Tonic - open source engine reimplemetation for Tonic Trouble

<img width="1800" height="1130" alt="Screenshot 2026-10-19 at 20 55 41" src="https://github.com/user-attachments/assets/ce53b698-d818-4161-8d94-40539f97c555" />

While still very much a work in progress, enough gameplay has been implemented to call this a working reimplementation of Tonic Trouble. Let's go through the folder structure:

* core - Tonic Core: a reimplementation of the Tonic Trouble engine. Done entirely assisted by Claude Code, formed from decompiled outputs and previous efforts to reverse engineer Rayman 2 (Open Rayman, namely)
* docs - various notes taken throughout the project, haphazard documentation, mostly Claude-speak
* files - where you'll be placing your own game assets and where the game's own files live
* sdl - an example SDL implementation of Tonic Core; currently slightly out of date with the Swift version
* Sources - what began as Tonic Viewer: a Swift program that wraps around TonicCore to render game levels natively via Metal APIs (you can still go into free roam view by pressing Tab; this was initially the default). It was the first target platform and the reason this repository is still a Swift package. The project evolved as I began iterating upon a "simple Ed walking mode".
* tools - Tonic Tools: all the scripts needed to extract and build game files. make_app.sh builds for macOS, build_linux.sh self-documents what it builds for. The output of the build scripts must obviously never be posted anywhere, it's filled to the brim with Ubisoft copyrighted assets.
  
 <img width="1800" height="1130" alt="Screenshot 2026-10-20 at 04 45 02" src="https://github.com/user-attachments/assets/aec86f85-30ff-4ddf-a167-1e21a0f756a7" /> 
 
The project is firmly in Alpha territory, with a lot of outstanding work still needing to be done. Just about everything is still rough around the edges, or completely unimplemented. For now it acts more like a proof of concept and exploration of the game itself. You can essentially simulate a playthrough by starting with Ski Slope (totalski) and sequentially going through portals. The next steps for the project consist of further decompilation and implementation of actual game mechanics. For now, it is what it is.

<img width="1800" height="1130" alt="skislopes" src="https://github.com/user-attachments/assets/4aceb873-a9d7-409e-9ad1-9a7b344dec09" />

### Controls

![Open Tonic controls](docs/controls.png)

Open this pictogram in the app by pressing F1.
