# AGENTS.md

## Purpose

This file is the working guide for coding agents and contributors operating inside `Plugins/ToolKitNetworking`.

The goal is to keep changes scoped to the networking plugin, aligned with the generated project that hosts it, and consistent with the upstream ToolKit engine implementation in:

`C:/Users/erendegirmenci/Desktop/Projects/GDTK`

## Working Area

Primary write scope:

- `Plugins/ToolKitNetworking/**`

Project-level read scope that matters for integration:

- `Codes/**`
- `CMakeLists.txt`
- `README.md`

Upstream engine read scope:

- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/**`

Do not edit the engine repo unless the task explicitly asks for engine changes.

## Repo Facts

- The generated project root is not a git repository in the current workspace.
- `Plugins/ToolKitNetworking` is its own git repository.
- The generated game project auto-discovers plugins by scanning `../Plugins/*/Codes` from `Codes/CMakeLists.txt`.
- The plugin can also be built directly from `Plugins/ToolKitNetworking/Codes/CMakeLists.txt`.
- Runtime plugin binaries are loaded from `Plugins/<PluginName>/Codes/Bin`.
- Plugin metadata is read from `Plugins/<PluginName>/Config/Plugin.settings`.

## Important Files

Plugin runtime and editor integration:

- `Codes/PluginMain.cpp`
- `Codes/PluginMain.h`
- `Config/Plugin.settings`
- `docs/plans/EDITOR_NETWORK_PLAY_PLAN.md`

Core networking systems:

- `Codes/NetworkManager.h`
- `Codes/NetworkManager.cpp`
- `Codes/NetworkComponent.h`
- `Codes/NetworkComponent.cpp`
- `Codes/NetworkBase.h`
- `Codes/NetworkBase.cpp`
- `Codes/NetworkPackets.h`
- `Codes/NetworkPackets.cpp`
- `Codes/NetworkState.h`
- `Codes/NetworkState.cpp`
- `Codes/NetworkSpawnService.h`
- `Codes/NetworkSpawnService.cpp`
- `Codes/NetworkRPCRegistry.h`
- `Codes/NetworkVariable.h`
- `Codes/NetworkMacros.h`

Embedded third-party transport:

- `Codes/enet/**`
- `.gitmodules`

Game-side example integration in this generated project:

- `Codes/Game.cpp`
- `Codes/NetworkPlayer.h`
- `Codes/NetworkPlayer.cpp`
- `Codes/CMakeLists.txt`

Useful upstream engine references:

- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/ToolKit/ToolKit.cpp`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/ToolKit/PluginManager.h`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/PluginWindow.cpp`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Templates/Game/Codes/CMakeLists.txt`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Templates/Plugin/Codes/CMakeLists.txt`

## Editor Multiplayer Direction

- The current direction for editor multiplayer is **not** extra editor processes.
- The local playable instance remains the in-editor simulation session.
- Additional clients and dedicated server remain standalone runtime/game processes.
- Launcher ownership is being moved toward upstream ToolKit editor/engine code; avoid adding more raw process-launch behavior to `PluginMain` unless the task explicitly requires temporary glue.
- See `docs/plans/EDITOR_NETWORK_PLAY_PLAN.md` for the current engine-owned launcher and simulation boot-package refactor plan.

## Runtime Model

- `PluginMain` is the DLL entrypoint exposed through `GetInstance()`.
- `OnLoad()` registers `NetworkComponent` and `NetworkManager` with the ToolKit object factory and editor metadata.
- `OnPlay()` looks for a scene entity that owns `NetworkManager`, then starts host, client, or dedicated-server mode.
- Launch mode is chosen from the component's `Role` plus optional command-line flags:
  `-server`, `-dedicated`, `-host`, `-client`, `-ip <addr>`, `-port <n>`.
- `NetworkManager` owns transport setup, packet dispatch, snapshots, RPC forwarding, spawn/despawn, and registered network component tracking.
- `NetworkComponent` is the replicated base class for game-side components such as `NetworkPlayer`.

## Build Rules

- Windows is the primary documented target for this plugin.
- The plugin CMake expects ToolKit engine headers and libraries through `TOOLKIT_DIR` or `%APPDATA%/ToolKit/Config/Path.txt`.
- Use forward slashes in `TOOLKIT_DIR` and in `Path.txt`.
- Editor/plugin outputs land in `Plugins/ToolKitNetworking/Codes/Bin`.
- Generated folders to treat as build artifacts:
  `build/`, `Intermediate/`, `x64/`, `Codes/Bin/`, `Codes/Intermediate/`, plugin `.vcxproj` outputs, and temporary hot-reload files under `Codes/Bin/tmp`.
- WSL agents may invoke Windows builds/tests through `cmd.exe` or `powershell.exe`, but should treat those as Windows-first verification paths, not native WSL builds.
- Run MSVC-backed targets serially from WSL. Do not start multiple builds from the same plugin build tree in parallel because shared `.pdb` files such as `ToolKitNetworkingCore.pdb` can trigger `C1041` locking failures.
- Short attached Windows commands usually work from WSL, but long attached plugin DLL builds can intermittently die in the bridge with `WSL ... UtilAcceptVsock ... accept4 failed 110`.
- For long standalone plugin builds, `ctest` runs, or generated-project builds from WSL, prefer launching a detached Windows process that logs to files under `.codex_tmp`, then poll the log and exit-code files from WSL instead of holding the interop session open.
- Use the repo-local helper scripts under [Scripts/run_windows_plugin_build.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_plugin_build.cmd), [Scripts/run_windows_ctest.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_ctest.cmd), and [Scripts/run_windows_project_build.cmd](/mnt/c/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Scripts/run_windows_project_build.cmd) instead of recreating ad hoc `.codex_tmp` wrappers.
- If plugin-tree CMake reconfigure suddenly stops finding ToolKit, check the real roaming AppData file `%APPDATA%\ToolKit\Config\Path.txt`; for this workspace it should contain `C:/Users/erendegirmenci/Desktop/Projects/GDTK`.

## Safe Editing Rules

- Keep edits focused on `Plugins/ToolKitNetworking` unless the task explicitly requires game-side sample updates.
- Do not commit or depend on generated artifacts from `build`, `Intermediate`, `x64`, or `Codes/Bin`.
- Do not remove the `Codes/enet` subtree or change its source origin casually; it is both vendored and referenced by CMake/`.gitmodules`.
- When documenting or fixing build issues, distinguish clearly between:
  game-project build flow,
  standalone plugin DLL build flow,
  editor runtime loading behavior.
- When modifying public behavior, check both plugin-side code and the sample game-side usage in `Codes/`.

## Expected Verification

Before considering work complete, verify the relevant subset of:

- CMake configure/build still points at the correct ToolKit engine path.
- Output DLL and import library appear in `Plugins/ToolKitNetworking/Codes/Bin`.
- Plugin metadata in `Config/Plugin.settings` still matches the plugin folder name.
- If runtime behavior changed, confirm `PluginMain`, `NetworkManager`, and a sample `NetworkComponent` flow still make sense together.
