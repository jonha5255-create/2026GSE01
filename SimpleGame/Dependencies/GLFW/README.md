# GLFW 3.4 vendored dependency

`glfw-3.4/` contains the complete, unmodified upstream GLFW 3.4 release source, headers, platform backends, CMake files, tests, examples and license. The game uses the Win32 backend and links GLFW statically through `glfw.vcxproj`.

- Upstream: https://github.com/glfw/glfw/tree/3.4
- Archive: https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
- Archive SHA-256: `A133DDC3D3C66143EBA9035621DB8E0BCF34DBA1EE9514A9E23E96AFD39FD57A`
- License: [zlib/libpng-style license](glfw-3.4/LICENSE.md), original notices retained.
- Included in the repository as ordinary files; no submodule, download step, package manager or separately installed GLFW is required.

Open `2026GSE01/SimpleGame.sln` with Visual Studio 2022 and the Desktop development with C++ workload plus Windows 10/11 SDK. Both Debug/Release and x86/x64 configurations build the matching `glfw3.lib` from source. `SimpleGame.vcxproj` has a project reference so building that project directly also builds its dependency. Output is under `build/<Platform>/<Configuration>/` and is ignored by Git. CRT selection matches the game (`/MD` or `/MDd`).

GLFW needs no runtime DLL in this configuration. GLEW remains the existing OpenGL extension loader; its architecture-matched import libraries and `glew32.dll` are already committed under `Dependencies/` and `Dependencies/Runtime/`. The runtime copy target copies only GLEW, not FreeGLUT. Historical FreeGLUT headers/libraries remain unused for reference; the application no longer links or calls them.

The game remains Windows-specific because its font and monitoring code use GDI/WinSock. Vendoring the cross-platform GLFW source does not by itself port those subsystems to Linux/macOS.

Verification script: `tools/build_glfw_matrix.ps1 -RuntimeChecks`. It discovers MSBuild using PATH or vswhere, rebuilds all four combinations in isolated game/GLFW object directories, and checks real contexts, resizing and cleanup. No developer-specific absolute paths appear in the build files.
