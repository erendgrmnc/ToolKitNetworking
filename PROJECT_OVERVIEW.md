# ToolKitNetworking Project Overview

## Summary

`ToolKitNetworking` is a ToolKit plugin that adds a server-authoritative multiplayer layer to a generated ToolKit project. It integrates with the engine's plugin system, object factory, scene/component model, and editor metadata system rather than modifying the engine core directly.

In this workspace, the plugin lives under:

`Plugins/ToolKitNetworking`

The upstream engine it targets lives under:

`C:/Users/erendegirmenci/Desktop/Projects/GDTK`

## Main Responsibilities

- Start and stop host, client, and dedicated-server networking modes
- Transport packets over ENet
- Replicate networked component state
- Dispatch RPCs across peers
- Track dirty network variables
- Spawn and despawn networked objects
- Register plugin-owned component types with the ToolKit editor/runtime

## Core Architecture

### Plugin Entry

`Codes/PluginMain.cpp` is the plugin entrypoint.

At load time it:

- exposes `GetInstance()` for the ToolKit plugin manager
- registers `NetworkComponent` and `NetworkManager` with the ToolKit object factory
- adds editor menu metadata so those components can appear in the editor

At play time it:

- scans the current scene for a `NetworkManager` component
- chooses a role from the component configuration plus command-line overrides
- starts server mode, client mode, or both

At stop or unload it:

- stops the active networking session
- unregisters plugin-owned component types

### NetworkManager

`NetworkManager` is the orchestration layer for the plugin.

It is responsible for:

- owning server and client transport objects
- processing incoming packets through the packet handler system
- maintaining the current server tick
- sending snapshots and client updates
- forwarding RPC payloads
- tracking registered `NetworkComponent` instances
- handling dynamic network object spawning and despawning through `NetworkSpawnService`

`NetworkManager` is also the bridge between the scene and the networking subsystem. Game-side code reaches it through `NetworkManager::Instance` and the component attached to a scene entity.

### NetworkComponent

`NetworkComponent` is the gameplay-facing base class for replicated components.

It provides:

- network identity and ownership tracking
- base serialization and deserialization hooks
- registration for `NetworkVariable` instances
- RPC handler registration and dispatch
- state history storage for snapshot-based updates
- support for dynamically spawned objects

The sample game-side `NetworkPlayer` in `Codes/NetworkPlayer.*` shows the expected usage pattern:

- derive from `NetworkComponent`
- register `NetworkVariable` members
- register RPC handlers
- drive local authority behavior
- let remote instances update from replicated state

### State, Packets, and RPCs

Supporting types are split across focused files:

- `NetworkPackets.*`
  packet types, message IDs, `PacketStream`, and serialization helpers
- `NetworkState.*`
  snapshot/state history bookkeeping
- `NetworkRPCRegistry.h`
  registry support for RPC dispatch across DLL boundaries
- `NetworkMacros.h`
  helper macros for reduced-boilerplate RPC registration/invocation
- `NetworkVariable.h`
  dirty-tracked replicated variable wrapper

Together they implement the plugin's core data flow:

1. local authoritative state changes
2. serialization into packets or snapshot data
3. transport over ENet
4. receive-side deserialization
5. state application or RPC invocation on the target component

### Transport Layer

`NetworkBase` wraps the low-level ENet host/peer interaction and packet handler registration.

`GameServer` and `GameClient` build on top of that layer to implement role-specific behavior.

The transport dependency is stored in `Codes/enet`, and the plugin CMake treats it as an embedded dependency.

## Runtime Modes

The plugin supports these runtime roles:

- `Client`
- `DedicatedServer`
- `Host`
- `None`

`PluginMain::ParseCommandLine()` recognizes these flags:

- `-server`
- `-dedicated`
- `-host`
- `-client`
- `-ip <address>`
- `-port <number>`

The effective runtime role comes from the `NetworkManager` component configuration and can be overridden from the command line when the plugin starts play mode.

## Integration With The Generated Project

This generated project's `Codes/CMakeLists.txt` auto-discovers plugins by scanning `../Plugins/*`.

For each plugin with a `Codes` directory, it:

- adds the plugin code directory to include paths
- adds nested `include` directories such as `Codes/enet/include`
- links against the plugin import library from `Plugins/<Plugin>/Codes/Bin`

That means `ToolKitNetworking` is integrated into this generated project without a manual plugin list.

The sample game-side code currently uses the plugin in two places:

- `Codes/Game.cpp`
  clears registered network components on play/stop and registers a spawn factory
- `Codes/NetworkPlayer.*`
  demonstrates a replicated gameplay component with a network variable and an RPC

## Build and Runtime Artifacts

- Plugin metadata:
  `Config/Plugin.settings`
- Plugin binaries:
  `Codes/Bin`
- Generated CMake and Visual Studio artifacts:
  `Codes/Intermediate`, `Intermediate`, `build`, `x64`

These generated folders should be treated as outputs, not source-of-truth inputs.
