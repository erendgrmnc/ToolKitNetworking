# Editor Network Play Implementation Plan

## Goal

Add Unreal-like editor network play support for ToolKit so developers can launch and test:

- `ListenServer + N players`
- `DedicatedServer + N players`
- `ClientAttach + N players`

This feature is for local editor testing only. It is not a broker, relay, or shipping matchmaking feature.

## Locked Decisions

- V1 is `DirectAddress` only.
- V1 is multi-process only.
- V1 is Windows-first.
- Child instances are standalone runtime/game processes, not extra full editor processes.
- The current editor process remains the primary playable local instance when applicable.
- `NetworkManager` remains the runtime networking source of truth in the scene.
- Editor network play settings are editor-owned workflow settings, not scene content.
- Exactly one active `NetworkManager` in the current scene is required for V1.

## Ownership Split

This feature spans the upstream ToolKit engine and the networking plugin. It is not implementable correctly in the plugin alone.

### Editor / Upstream ToolKit

Own:

- network play settings UI inside the existing simulation/play settings flow
- durable persistence for editor network play settings
- launching standalone child processes
- per-instance config/workspace isolation
- runtime child boot handoff into the correct project/scene/play mode

Primary engine touch points to use:

- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/App.cpp`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/SimulationWindow.cpp`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/SimulationWindow.h`
- `/mnt/c/users/erendegirmenci/toolkit/eren/Codes/Main.cpp`

### Networking Plugin

Own:

- scene validation for network play
- launch-manifest generation for network roles and ports
- local in-editor networking startup and shutdown
- child launch bookkeeping and teardown on play stop
- reuse of existing runtime networking config from `NetworkManager`

Primary plugin touch points:

- [PluginMain.cpp](/mnt/c/users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/PluginMain.cpp)
- [PluginMain.h](/mnt/c/users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/PluginMain.h)
- [NetworkManager.h](/mnt/c/users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkManager.h)
- [NetworkSessionCore.cpp](/mnt/c/users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionCore.cpp)

## Runtime Contract

Child instances must not rely on inheriting editor state. V1 uses an explicit manifest-based launch contract.

### Manifest Path

- Generate one play-session folder under:
  - `Intermediate/NetworkPlay/<launch-id>/`
- Generate one XML manifest per child instance under:
  - `Intermediate/NetworkPlay/<launch-id>/instances/<instance-id>.settings`

### Child Launch Arguments

Pass:

- `-networkPlayManifest=<absolute path>`
- `-headless` for dedicated server children

Do not rely on raw `-server` / `-client` / `-ip` / `-port` arguments alone for editor-launched child instances. Those remain valid for manual runs, but editor play should use the manifest as the authoritative handoff.

Current status:

- runtime boot groundwork currently consumes `configRoot`, `resourceRoot`, `projectRoot`, `scenePath`, `headless`, and `autoPlay`
- broader per-instance launch/session fields are intentionally deferred to plugin-side orchestration and the later manifest expansion work
- Phase 2 should be read as “runtime boot groundwork complete”, not “final manifest contract complete”

### Manifest Contents

Each child manifest must contain:

- launch id
- instance id
- topology
- child role:
  - `ListenServerHost`
  - `DedicatedServer`
  - `Client`
- project root
- scene path
- isolated config root
- isolated workspace root if required by editor/runtime boot
- auto-play flag
- headless flag
- network join method
- connect host
- connect port
- listen port
- bind address
- advertised address
- session id
- join credential
- build compatibility id
- max clients when hosting

V1 note:

- not every field above must be consumed by the runtime immediately
- Phase 2 only requires the subset needed for isolated config/resource boot and scene handoff
- the remaining launch/session fields become actionable when Phase 3 plugin-side manifest generation lands

### Runtime Boot Requirements

`Codes/Main.cpp` must gain a child-network-play bootstrap path that:

1. detects `-networkPlayManifest`
2. loads the manifest before normal play startup
3. redirects config/workspace state to the isolated per-instance roots
4. opens the requested project/scene
5. auto-enters play
6. allows the existing networking plugin runtime flow to start normally from the scene `NetworkManager`

Current implementation boundary:

- runtime boot currently redirects config/resource roots and loads the requested scene before normal play startup
- workspace-root redirection is still optional and should only be added if child instances prove they need stricter workspace isolation than config/resource separation provides

## Settings Model

Store editor network play settings in project-level editor config, not plugin reload XML and not scene component params.

### Durable Settings Location

Use the project editor settings file:

- `/mnt/c/users/erendegirmenci/toolkit/eren/Config/Editor.settings`

Add a dedicated network play settings block alongside existing simulation/editor window state.

### Settings Fields

- `enabled`
- `topology`
- `playerCount`
- `runDedicatedServerHeadless`
- `autoStopChildren`
- `basePort`
- `autoAllocatePorts`

Rules:

- `playerCount` means playable clients, not total processes
- `DedicatedServer` topology launches one extra headless server process in addition to clients
- `ListenServer` topology counts the local editor instance as player 1
- `ClientAttach` topology requires an explicit remote target in the scene `NetworkManager`

## Step-By-Step Implementation

### Phase 1: Add Editor Network Play Settings

- [x] Extend upstream ToolKit simulation/play settings UI to include a networking section
- [x] Add a durable `NetworkPlaySettings` block in `Config/Editor.settings`
- [x] Load and save those settings through the existing editor app/simulation flow
- [x] Keep the settings editor-owned; do not add player-count/topology fields to `NetworkManager`

Done when:

- the editor can configure topology and player count without touching scene content
- those settings persist across editor restarts

### Phase 2: Add Runtime Child Boot Handoff

- [x] Add `-networkPlayManifest=<path>` handling in `Codes/Main.cpp`
- [x] Define the XML manifest schema and parser
- [x] Add project/scene boot handoff so a child instance opens the requested scene before play
- [x] Add isolated config/workspace redirection for each child instance
- [x] Keep normal manual boot behavior unchanged when no manifest is provided

Done when:

- a launched child runtime can boot the same scene automatically and enter play without user interaction

Implementation notes:

- The active project runtime now supports `-networkPlayManifest=<path>` and `-headless`.
- The upstream game template runtime was updated to mirror the same boot contract for future generated projects.
- Invalid or missing manifest files now fail fast instead of being silently ignored.

### Phase 3: Add Plugin-Orchestrated Launch Sessions

- [x] Extend `PluginMain` to validate the scene before play
- [x] Require exactly one active `NetworkManager`
- [x] Reject unsupported join methods for editor network play with a clear message
- [x] Build a launch manifest set from editor settings + scene `NetworkManager`
- [x] Spawn child runtime processes and track their handles
- [x] Tear down all child processes on `OnStop()` and `OnUnload()`

Done when:

- starting play launches the expected child processes and stopping play cleans them up

Implementation notes:

- `PluginMain` now validates active project, saved scene, runtime executable presence, supported join method, and single-manager ownership before starting play.
- Child instances are launched through `CreateProcessW` and tracked by process handle for later cleanup.
- The local editor `NetworkManager` is temporarily overridden for the selected topology and restored on stop/unload.

### Phase 4: Topology Mapping

- [x] Implement `ListenServer + N players`
  - local editor instance is host player
  - launch `N - 1` client children
- [x] Implement `DedicatedServer + N players`
  - launch 1 headless dedicated server child
  - local editor instance is client 1
  - launch `N - 1` client children
- [x] Implement `ClientAttach + N players`
  - local editor instance is client 1
  - launch `N - 1` client children
  - do not launch a host/server child
- [x] Derive deterministic ports/session id per play session

Done when:

- all three topologies work locally through the editor play flow

Implementation notes:

- Topology mapping currently derives deterministic local ports from the editor play settings and uses loopback routing for local host/dedicated flows.
- DirectAddress is the only supported join method for the first editor-launch slice.

### Phase 5: Failure Handling

- [x] Abort play if manifest generation fails
- [x] Abort play if any required child process fails to launch
- [x] Stop already-launched children on partial launch failure
- [x] Reject scenes with zero or multiple `NetworkManager` instances
- [x] Reject non-`DirectAddress` join methods for V1

Done when:

- the editor never enters a half-started orphaned network play session silently

Implementation notes:

- Abort is deferred through the editor frame loop so `OnPlay()` does not reenter stop logic directly.
- Child cleanup is best-effort and immediate on partial launch failure.

### Phase 6: Tests And Verification

- [x] Add unit-like coverage for launch-manifest generation
- [x] Add unit-like coverage for topology-to-instance mapping
- [x] Add runtime boot coverage for manifest parsing and manifest-path detection
- [x] Add runtime boot coverage for child auto-play startup
- [x] Add integration coverage for child process tracking and teardown
- [x] Add acceptance checks for:
  - listen server with 2 players
  - dedicated server with 3 players
  - client attach with 2 players

Implementation notes:

- Launch/topology planning now lives in a shared `EditorNetworkPlayPlanner` helper instead of staying embedded in `PluginMain`.
- Engine-coupled tests now cover:
  - listen-server planning
  - dedicated-server planning
  - client-attach validation
  - manifest XML serialization and escaping
  - runtime manifest parsing
  - runtime manifest path detection
  - runtime auto-play and headless boot decisions
  - plugin child launch tracking and teardown
  - deferred abort cleanup and override restoration
  - acceptance-style role/launch coverage for listen-server, dedicated-server, and client-attach flows

## Acceptance Criteria

- Choosing `ListenServer` with `2` players gives:
  - 1 local editor host player
  - 1 launched client runtime child
- Choosing `DedicatedServer` with `3` players gives:
  - 1 headless dedicated server child
  - 1 local editor client
  - 2 launched client runtime children
- Choosing `ClientAttach` with `2` players gives:
  - 1 local editor client
  - 1 launched client runtime child
- Stopping editor play terminates all launched children
- Child instances do not overwrite the editor’s shared config/workspace state

## Explicit Out Of Scope

- `SessionDirectory` / broker-backed editor launch
- relay or NAT traversal
- same-process multi-window PIE
- multiple `NetworkManager` arbitration
- shipping build matchmaking or invites

## Directions For Coding Agents

- Do not implement this as a plugin-only feature. It requires upstream ToolKit editor/runtime changes.
- Do not store editor network play settings on `NetworkManager`.
- Do not use plugin `OnLoad/OnUnload(XmlDocumentPtr state)` reload XML as the primary persistence path.
- Do not make child instances depend on inherited editor state; use the manifest contract.
- Keep V1 scoped to `DirectAddress`.
- Preserve existing manual CLI networking behavior outside editor network play.


## Current Direction Update

The first implementation slice proved that the basic multi-process model works, but it also exposed the wrong ownership boundary.

### Confirmed Direction

- Keep the **current editor instance** as the local authoritative play session owner.
- Keep **additional clients** and **dedicated server** as **standalone runtime/game processes**.
- Do **not** try to launch extra editor simulations or extra `Editord.exe` instances for V1.
- Do **not** keep process launch, dependency staging, and isolated child runtime boot fully owned by the networking plugin.

### Why This Changed

The current ToolKit editor architecture is single-session:

- one editor play lifecycle
- one simulation viewport
- one active scene/runtime state

That makes “another editor simulation” the wrong target for extra clients on this branch.

At the same time, the current plugin-owned launcher path in `PluginMain.cpp` is too low-level. The recent failures around `SDL2d.dll`, `ToolKitNetworkingd.dll`, `Editord.exe`, and child startup ordering are all symptoms of runtime-launch ownership living in the wrong layer.

### New Ownership Split

#### Engine / Editor Owns

- runtime executable resolution
- launch working directory
- per-instance config/temp/log isolation
- runtime dependency staging
- child process launch / teardown primitives
- simulation boot package authoring at the editor/runtime boundary

#### Networking Plugin Owns

- topology planning
- per-instance network role and port mapping
- scene validation for editor network play
- local in-editor `NetworkManager` override/start/stop behavior
- manifest / boot-package networking fields

## Next Refactor Phases

### Phase 7: Engine-Owned Runtime Launcher

Add an upstream editor/runtime launcher abstraction, for example:

- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/EditorRuntimeLauncher.h`
- `C:/Users/erendegirmenci/Desktop/Projects/GDTK/Editor/EditorRuntimeLauncher.cpp`

It should own:

- runtime executable resolution from the active workspace/project
- launch working directory
- child process creation
- process handle tracking primitives
- per-instance runtime staging helpers

Status:

- [x] upstream launcher seam added
- [x] `PluginMain` no longer calls `CreateProcessW` directly
- [x] `PluginMain` no longer owns runtime executable resolution
- [x] `PluginMain` no longer owns raw child launch command-line construction
- [x] per-instance runtime staging helpers for config/temp/log roots are in place

Implementation notes:

- The first cut of `EditorRuntimeLauncher` is currently header-backed so the plugin can consume the upstream launcher seam without waiting on a separate `Editord` binary rebuild.
- This phase intentionally moves process-launch ownership first; config authoring, plugin/runtime staging, and scene snapshot handoff remain later phases.

### Phase 8: Simulation Boot Package

Upgrade the current child manifest into a real simulation boot package under:

- `Intermediate/NetworkPlay/<launch-id>/`

Each child package should include:

- manifest
- isolated config root
- isolated temp/log root
- scene snapshot path
- project/resource roots
- runtime/plugin load directives
- network role / host / port / session overrides

Status:

- [x] manifest now carries snapshot scene path, workspace/config/resource roots, temp/log roots, and runtime plugin directives
- [x] child config roots now get an explicit `Engine.settings` authored for runtime boot
- [ ] boot package still uses a lightweight XML manifest rather than a wider multi-file package descriptor

Done when:

- child runtime boot no longer depends on the last saved source scene alone
- child runtime has all required runtime/plugin/config state from the boot package

### Phase 9: Scene Snapshot Handoff

The editor must serialize the **current open scene** for the play session.

Rules:

- local editor instance keeps using its in-memory scene
- child runtimes load a temporary snapshot scene generated for the play session
- child snapshot path becomes authoritative over raw `scenePath`

Status:

- [x] editor network play now writes a temporary scene snapshot under `Intermediate/NetworkPlay/<launch-id>/Scenes/Current.scene`
- [x] child runtimes now prefer the snapshot scene over the last saved source `scenePath`

Done when:

- unsaved scene edits are reflected consistently across launched multiplayer instances

### Phase 10: Child Runtime Plugin / Config Authoring

Generate child runtime config explicitly instead of loosely copying whatever editor config tree exists.

Requirements:

- child `Engine.settings` must contain the correct runtime plugin set
- runtime-relevant plugin state must be explicit
- editor-only config/layout state must not leak into child boot

Status:

- [x] child `Engine.settings` is now authored explicitly per instance during launch
- [x] child runtime plugin list now comes from loaded non-editor plugins, excluding the editor-only networking launcher plugin
- [x] project game code now registers `NetworkManager` / `NetworkComponent` for standalone runtime scene load before child scene boot

Done when:

- child instances can load required runtime plugins deterministically
- child boot no longer depends on incidental editor/AppData state

### Phase 11: Plugin Refactor To Use Engine Launcher

Refactor the networking plugin to consume the engine-owned launcher.

`PluginMain` should provide:

- `NetworkPlaySessionSpec`
- per-instance network boot data
- validation errors

`PluginMain` should stop owning:

- raw process creation
- direct dependency staging assumptions
- direct executable-path assumptions
- direct per-instance config copying

Status:

- [x] raw process launch and runtime executable resolution moved out of `PluginMain` into the upstream editor launcher seam
- [ ] manifest writing, scene snapshot authoring, and child config authoring still live in the networking plugin for now

Done when:

- networking plugin is responsible for networking orchestration only
- engine/editor is responsible for runtime launch and child lifecycle mechanics

## Updated Acceptance Criteria

This feature should now be considered architecture-complete only when:

- local editor instance runs the authoritative in-editor simulation session
- extra clients/server run as standalone runtime processes
- runtime launch is engine-owned, not plugin-owned
- child boot uses a simulation boot package, not only raw scene path + flags
- child instances can reproduce current editor scene state through snapshot handoff
- child runtime config/plugin state is explicit and deterministic

## Guardrails For Coding Agents

- Do not pivot the feature toward spawning extra editor processes.
- Do not add more Win32 process-launch logic into `Plugins/ToolKitNetworking/Codes/PluginMain.cpp` unless it is strictly temporary glue for the refactor.
- Treat standalone runtime children as the correct target.
- Prefer moving launcher mechanics into upstream engine/editor code before adding more plugin-side child boot complexity.
