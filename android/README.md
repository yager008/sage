# Android Port Plan

This directory will host the upcoming Android/NDK port of VEngine. The Win32 and WebGL versions stay untouched in their respective roots. The port roadmap:

1. Set up an SDL2 + OpenGL ES 3 rendering core that mirrors the desktop renderer.
2. Implement Android-specific input (touch controls, virtual buttons).
3. Wire ImGui using `imgui_impl_android` + `imgui_impl_opengl3`.
4. Provide Gradle + CMake scripts for building an APK with the NDK.
