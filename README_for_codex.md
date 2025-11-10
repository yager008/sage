# Codex Change Log

This document is maintained by the Codex agent to explain every code change, rationale, and related logic adjustments performed through the assistant.

## 2025-11-02

- Session: project analysis only.
- Actions: reviewed existing OpenGL project structure, gameplay loop, and rendering pipeline.
- Code changes: none.
- Notes: README initialized to capture future modifications executed via Codex.

### Change Set – Grid Controls & Snow

- Added 10° top-down camera transform, grid floor rendering, and per-cell cube translation to prepare for tactical movement (`src/main.cpp` camera/grid helpers).
- Bound arrow keys to discrete grid movement while retaining jump physics; introduced tweened motion state to keep animation smooth.
- Implemented pixelated snowfall by spawning particles in retro render buffer before post-process blit, matching requested aesthetic.
- Rationale: align demo with new requirements (overhead view, navigable grid, seasonal ambiance) without overhauling existing rendering pipeline.

### Change Set – Code Panel

- Embedded Win32 `EDIT` control along left edge with responsive layout to allow free-form code entry beside the OpenGL view (`src/main.cpp` window setup and resize handling).
- Persisted note contents to `notes.txt` beside the executable using `GetModuleFileNameA`, loading on startup and flushing on close to satisfy user persistence requirement.
- Adjusted rendering pipeline to reserve horizontal space for the panel when computing retro buffer size, post-process offsets, and snow overlay dimensions.
- Rationale: give players/designers an in-app scratchpad without disrupting existing rendering flow.

### Change Set – Code Panel Simplification

- Removed experimental Vim-mode controls and checkbox, returning the code editor to default Win32 behaviour for predictability (`src/main.cpp` input handling and window setup).
- Retained the dedicated code panel but refreshed its fill routine so the background renders solid white behind text, avoiding the default blue tint on themed systems.
- Cleaned up auxiliary Vim-specific utilities and subclassing hooks to keep the codebase minimal.
- Rationale: user requested the classic text box feel without advanced keybindings while preserving readability improvements.

### Change Set – Dear ImGui Code Window

- Replaced the Win32 edit control with an integrated Dear ImGui context rendered on top of the OpenGL scene, creating a dedicated "Code" window with multiline editing (`src/main.cpp`, ImGui setup/rendering path).
- Wired the new UI into the existing text persistence pipeline, keeping auto-load/save of `notes.txt` and adding an explicit in-window "Save" action.
- Updated the build to compile ImGui core and Win32/OpenGL backends, adding necessary include paths and libraries in `Makefile`.
- Polished the code UI into a fixed-size, slide-in panel with a toggle button and animated hide/show so users can reclaim screen space without losing scroll state.
- Ensured the slide-in code panel resizes with the window while sticking with ImGui's default font to keep the setup portable.
- Added a "Docs" toggle in the overlay so the sliding panel can swap between editable code and a read-only engine guide, with selectable text and a copy-to-clipboard shortcut.
- Enabled smooth WASD panning, mouse-wheel zoom, and grid cube placement/removal via mouse clicks; camera now uses gluLookAt for a tilted orbit.
- Added a bottom "Content" panel that slides up like Unreal's browser and lets you choose between blue/red/grey cube presets when spawning.
- Painted the 3D view with an adjustable blue/pink gradient and exposed an Environment panel with live color pickers.
- Reworked world interaction so blocks have full 3D coordinates: left click adds to any face (including new floor blocks below the grid), right click removes the top-most block in a column, and the player now walks, jumps, and collides smoothly against the tower of blocks.
- Camera rotation can now be smoothly tweened in 90° increments via the `Q`/`E` keys while orbiting around the world origin; character movement re-aligns with the camera heading.
- Added a `webgl/` target that rewrites the engine around SDL2/OpenGL ES 3 + ImGui backends so it can be compiled with Emscripten and embedded in a browser. Includes shader-based rendering, the same code/docs/content overlays, Lua syntax highlighting, and cube placement logic ported to modern GL.

### Change Set – Texture System & Scene Persistence

- Added PNG texture loading per preset inside the Content Browser. Each block type has a path field (`texture.png` hint), **Load PNG** button that normalises paths relative to the exe folder, and **Clear** to reset. Status text and resolution readout confirm successful uploads. (Win32 only for now.)
- Introduced a drag-and-drop editor: long-press LMB on any cube to grab it, a violet outline tracks valid placement, and the block follows the cursor until released. Invalid drops snap back; drag state uses `SetCapture` so you can move outside the window.
- All placed cubes now remember `presetIndex`, glow/transparent flags, texture handle, and relative texture path. Scene state persists to `scene.txt` (same directory as the exe). On startup, cubes + textures auto-load; on close, any modifications flush to disk after the notes save.
- Updated controls overlay/docs to mention R/F pitch adjustments, Content toggle, texture workflow, and the new `C` hotkey for showing the Content Browser.
