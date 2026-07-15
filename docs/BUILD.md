# Building ToolKitNetworking

## Summary

There are two supported build paths for this workspace:

1. Build the generated game project, which auto-discovers and links `ToolKitNetworking`.
2. Build the plugin directly as a ToolKit editor/runtime DLL from `Plugins/ToolKitNetworking/Codes`.

If you are working from WSL, you can still invoke the Windows toolchain through `cmd.exe` or `powershell.exe` and use the same documented Windows working directories and commands.

Both paths depend on a working ToolKit engine checkout at:

`C:/Users/erendegirmenci/Desktop/Projects/GDTK`

## Prerequisites

- Windows
- Visual Studio 2022 with MSVC C++ tools
- CMake 3.14 or newer
- A built ToolKit engine and dependencies in `C:/Users/erendegirmenci/Desktop/Projects/GDTK`
- ToolKit path configured either through:
  - environment variable `TOOLKIT_DIR`, or
  - `%APPDATA%/ToolKit/Config/Path.txt`

Important:

- Use forward slashes in the ToolKit path.
- The plugin CMake checks for `ToolKit/ToolKit.h` under `TOOLKIT_DIR`.
- The engine and plugin expect matching build configurations, especially `Debug` vs `Release`.

Recommended `Path.txt` contents:

```text
C:/Users/erendegirmenci/Desktop/Projects/GDTK
```

## Before Building

Make sure the engine has already produced the libraries that the project and plugin link against, including:

- `ToolKit[d].lib`
- `Editor[d].lib`
- `imgui[d].lib`
- dependency outputs under `Dependency/Intermediate/Windows/<Config>`

If those are missing, build the engine first from the upstream repo:

1. Open `C:/Users/erendegirmenci/Desktop/Projects/GDTK`
2. Run `BuildDependencies.bat`
3. Build the engine solution or the required targets in Visual Studio 2022

## Option 1: Build The Generated Project

Use this path when you want to build the sample game/plugin combination in this workspace.

From the generated project root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTOOLKIT_DIR="C:/Users/erendegirmenci/Desktop/Projects/GDTK"
cmake --build build --config Debug
```

What this does:

- configures the generated project from the root `CMakeLists.txt`
- builds the game code under `Codes`
- auto-discovers plugins from `Plugins/*/Codes`
- adds `Plugins/ToolKitNetworking/Codes` and `Plugins/ToolKitNetworking/Codes/enet/include` to include paths
- links against `ToolKitNetworkingd.lib` from `Plugins/ToolKitNetworking/Codes/Bin`

Expected outputs:

- game binary and import library under `Codes/Bin`
- plugin binary and import library under `Plugins/ToolKitNetworking/Codes/Bin`

Notes for this workspace:

- `Codes/CMakeLists.txt` is already customized to auto-discover plugins, so no manual plugin list is needed.
- Existing generated build files indicate Visual Studio 2022 is already the active generator for this project.

## Option 2: Build The Plugin Directly

Use this path when you want to iterate on `ToolKitNetworking` as a standalone plugin DLL.

From the generated project root:

```powershell
cmake -S Plugins/ToolKitNetworking/Codes -B Plugins/ToolKitNetworking/Intermediate/PluginBuild -G "Visual Studio 17 2022" -A x64 -DTOOLKIT_DIR="C:/Users/erendegirmenci/Desktop/Projects/GDTK"
cmake --build Plugins/ToolKitNetworking/Intermediate/PluginBuild --config Debug
```

What this does:

- configures the plugin's own `Codes/CMakeLists.txt`
- links against ToolKit engine, editor, imgui, and ENet
- emits the plugin DLL, import library, and PDB into `Plugins/ToolKitNetworking/Codes/Bin`

Expected outputs:

- `Plugins/ToolKitNetworking/Codes/Bin/ToolKitNetworkingd.dll`
- `Plugins/ToolKitNetworking/Codes/Bin/ToolKitNetworkingd.lib`
- `Plugins/ToolKitNetworking/Codes/Bin/ToolKitNetworkingd.pdb`

## ENet Dependency

The plugin stores ENet under:

`Plugins/ToolKitNetworking/Codes/enet`

The plugin repo also contains:

`Plugins/ToolKitNetworking/.gitmodules`

If `Codes/enet` is empty in a fresh clone, initialize submodules from the plugin repo before treating it as a CMake problem:

```powershell
git -C Plugins/ToolKitNetworking submodule update --init --recursive
```

The plugin CMake also uses `FetchContent` with `SOURCE_DIR` pointing at that folder, so a populated local ENet tree is the cleanest starting point.

## Runtime Loading Expectations

ToolKit loads plugins from:

- `Plugins/<PluginName>/Codes/Bin`
- `Plugins/<PluginName>/Config/Plugin.settings`

For this plugin that means:

- binary path:
  `Plugins/ToolKitNetworking/Codes/Bin`
- metadata path:
  `Plugins/ToolKitNetworking/Config/Plugin.settings`

If the plugin builds but does not load in the editor, verify:

- the DLL name matches the plugin folder name
- `Plugin.settings` still names the plugin `ToolKitNetworking`
- you built the same configuration that the editor is loading
- the engine path in `TOOLKIT_DIR` or `Path.txt` is correct

## Common Problems

### ToolKit path not found

Symptom:

- CMake reports it cannot find `ToolKit/ToolKit.h`

Fix:

- set `TOOLKIT_DIR` explicitly, or
- create `%APPDATA%/ToolKit/Config/Path.txt`, and
- use forward slashes instead of backslashes

### Engine libraries missing

Symptom:

- link errors for `ToolKit`, `Editor`, `imgui`, or dependency libraries

Fix:

- build engine dependencies with `BuildDependencies.bat`
- build the ToolKit engine/editor targets in the upstream engine repo
- rebuild using the same configuration as the missing libraries

### ENet include or source issues

Symptom:

- missing `enet/...` headers or ENet target configuration failures

Fix:

- confirm `Plugins/ToolKitNetworking/Codes/enet` is populated
- initialize plugin submodules if needed

### Plugin builds but the game project does not link it

Symptom:

- unresolved external symbols for `ToolKitNetworking`

Fix:

- confirm the plugin library exists in `Plugins/ToolKitNetworking/Codes/Bin`
- reconfigure the root project so `Codes/CMakeLists.txt` re-discovers the plugin
- build both the project and plugin in the same configuration

## WSL Interop Note

When building from WSL through Windows interop:

- prefer `cmd.exe /c ...` or `powershell.exe -NoProfile -Command ...` against the documented Windows paths
- run MSVC-backed build targets serially
- do not launch `ToolKitNetworking_unit_tests`, `ToolKitNetworking_integration_tests`, and plugin DLL targets in parallel from the same build tree because shared `.pdb` outputs can cause `C1041` locking failures
- short commands usually work fine attached, but long attached Windows builds from WSL can intermittently fail with `WSL ... UtilAcceptVsock ... accept4 failed 110`
- for long plugin DLL builds or `ctest` runs, prefer a detached Windows process that writes stdout/stderr to a log file and writes `%ERRORLEVEL%` to a separate exit-code file, then poll those files from WSL
- repo-local helper scripts for this live under [Scripts/run_windows_plugin_build.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_plugin_build.cmd), [Scripts/run_windows_ctest.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_ctest.cmd), and [Scripts/run_windows_project_build.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_project_build.cmd)

Detached pattern used successfully in this workspace:

```cmd
Plugins\ToolKitNetworking\Scripts\run_windows_plugin_build.cmd
```

Launch it from WSL with:

```powershell
Start-Process -FilePath 'C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Scripts\run_windows_plugin_build.cmd' `
  -WindowStyle Hidden
```

For tests:

```powershell
Start-Process -FilePath 'C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Scripts\run_windows_ctest.cmd' `
  -WindowStyle Hidden
```

For the generated project build:

```powershell
Start-Process -FilePath 'C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Scripts\run_windows_project_build.cmd' `
  -WindowStyle Hidden
```

Poll these log and exit files from WSL:

- `.codex_tmp/plugin_build.log`
- `.codex_tmp/plugin_build.exit`
- `.codex_tmp/plugin_ctest.log`
- `.codex_tmp/plugin_ctest.exit`
- `.codex_tmp/project_build.log`
- `.codex_tmp/project_build.exit`

Path resolution note:

- if a plugin-tree reconfigure unexpectedly stops finding ToolKit, check the real roaming AppData file at `%APPDATA%\ToolKit\Config\Path.txt`
- expected contents for this workspace:
  `C:/Users/erendegirmenci/Desktop/Projects/GDTK`

## Required Testing Steps

Use this verification sequence for networking changes:

1. Build unit tests:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_unit_tests
```

2. Build session/integration tests:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_integration_tests
```

3. For replication or transport-security changes, also build engine-coupled security tests:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake -S Plugins/ToolKitNetworking/Codes -B Plugins/ToolKitNetworking/Intermediate/Plugin `
  -G "Visual Studio 17 2022" -A x64 `
  -DTK_NET_BUILD_TESTS=ON -DTK_NET_BUILD_ENGINE_TESTS=ON
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_engine_tests
```

4. Run all enabled plugin tests:
```powershell
ctest -C Debug --output-on-failure
```

5. Build the generated project:
```powershell
cmake --build Intermediate\Plugin --config Debug
```

6. Build the standalone plugin DLL:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking
```

Notes:

- steps 1, 2, 3, 5, and 6 should be run serially from WSL
- step 3 is required when editing `ReplicationManager`, transport-facing auth logic, or pre-auth packet handling
