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

- Removed experimental Vim-mode controls and checkbox, returning the notes editor to default Win32 behaviour for predictability (`src/main.cpp` input handling and window setup).
- Retained the dedicated notes panel but refreshed its fill routine so the background renders solid white behind text, avoiding the default blue tint on themed systems.
- Cleaned up auxiliary Vim-specific utilities and subclassing hooks to keep the codebase minimal.
- Rationale: user requested the classic text box feel without advanced keybindings while preserving readability improvements.

### Change Set – Dear ImGui Code Window

- Replaced the Win32 edit control with an integrated Dear ImGui context rendered on top of the OpenGL scene, creating a dedicated "Code" window with multiline editing (`src/main.cpp`, ImGui setup/rendering path).
- Wired the new UI into the existing text persistence pipeline, keeping auto-load/save of `notes.txt` and adding an explicit in-window "Save" action.
- Updated the build to compile ImGui core and Win32/OpenGL backends, adding necessary include paths and libraries in `Makefile`.
- Polished the notes UI into a fixed-size, slide-in panel with a toggle button and animated hide/show so users can reclaim screen space without losing scroll state.
- Ensured the slide-in notes panel resizes with the window while sticking with ImGui's default font to keep the setup portable.
- Added a "Docs" toggle in the overlay so the sliding panel can swap between editable notes and a read-only engine guide, with selectable text and a copy-to-clipboard shortcut.
- Enabled smooth WASD panning, mouse-wheel zoom, and grid cube placement/removal via mouse clicks; camera now uses gluLookAt for a tilted orbit.
- Added a bottom "Content" panel that slides up like Unreal's browser and lets you choose between blue/red/grey cube presets when spawning.
- Painted the 3D view with a subtle blue-to-pink gradient background instead of a flat clear colour.
- Renamed the overlay/panel to "Code" and layered a lightweight Lua syntax highlighter over the editor to make scripts easier to read.
- Rationale: fulfill request for an ImGui-based code window while maintaining the retro render pipeline and platform independence of the rest of the app.
