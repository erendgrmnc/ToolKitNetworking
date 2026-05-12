# Contributing to ToolKitNetworking

## Overview

`ToolKitNetworking` is a ToolKit editor/runtime plugin that provides ENet-based transport, server-authoritative replication, RPC dispatch, and dynamic spawn support for game-side components derived from `NetworkComponent`.

This document covers the expected workflow for changes inside `Plugins/ToolKitNetworking`.

## Prerequisites

- Windows development environment
- Visual Studio 2022 with C++ workload
- CMake 3.14 or newer
- A built ToolKit engine at `C:/Users/erendegirmenci/Desktop/Projects/GDTK`
- `TOOLKIT_DIR` configured directly or via `%APPDATA%/ToolKit/Config/Path.txt`
- Forward-slash ToolKit path, for example:
  `C:/Users/erendegirmenci/Desktop/Projects/GDTK`

## Where To Change What

Use these boundaries when choosing where to work:

- `Codes/PluginMain.*`
  Plugin lifecycle, editor registration, play/stop startup flow, command-line mode parsing.
- `Codes/NetworkManager.*`
  Host/client startup, transport, tick/update loop, packet routing, snapshots, spawn service access, registered component tracking.
- `Codes/NetworkComponent.*`
  Base replicated component behavior, transform/state serialization, network-variable handling, RPC dispatch.
- `Codes/NetworkPackets.*`
  Packet structures, `PacketStream`, serializer/deserializer helpers, message layout.
- `Codes/NetworkState.*`
  Snapshot history/state bookkeeping.
- `Codes/NetworkSpawnService.*`
  Dynamic network object registration and spawning.
- `Codes/NetworkVariable.h`
  Dirty tracking and replicated field serialization.
- `Codes/NetworkMacros.h`
  RPC convenience macros and dispatch helpers.
- `Config/Plugin.settings`
  Plugin metadata surfaced by the editor.

If you need an example of plugin consumption, inspect the generated project sample:

- `Codes/Game.cpp`
- `Codes/NetworkPlayer.*`

## Recommended Workflow

1. Build or inspect the upstream ToolKit engine first if the change depends on engine-side contracts.
2. Make plugin changes inside `Plugins/ToolKitNetworking`.
3. If the change affects consumer usage, update the sample game-side code or docs as needed.
4. Build the plugin directly, or build the generated project that links it.
5. Verify the runtime path that the change touches: load, play, connect, replicate, RPC, spawn, stop.

## Coding Expectations

- Preserve the existing plugin boundary: ToolKit engine APIs are consumed, not redefined here.
- Keep serialization and packet format changes deliberate and documented.
- Treat `NetworkComponent` as the public gameplay extension point.
- Prefer explicit behavior over hidden magic; networking bugs are hard enough without unclear control flow.
- Keep editor-only concerns in `PluginMain` and build settings, not in gameplay-facing types.
- Be careful with hash-based RPC registration and DLL-boundary assumptions.
- Do not introduce path handling that assumes backslashes in CMake configuration values.

## Adding or Changing Replicated Gameplay Code

For a new replicated gameplay component:

1. Derive from `ToolKitNetworking::NetworkComponent`.
2. Register `NetworkVariable` members in the constructor.
3. Register RPC handlers in the constructor, or use the RPC macros consistently.
4. Override `Serialize()` and `Deserialize()` only when the base replication flow is not enough.
5. Register the type with the ToolKit object factory and, if it is dynamically spawned, with `NetworkManager::RegisterSpawnFactory<T>()`.

When changing replication behavior:

- Check snapshot compatibility between sender and receiver.
- Review whether `NetworkManager`, `NetworkComponent`, and `NetworkState` must all change together.
- Confirm whether the client-side interpolation/extrapolation settings still match the packet content being sent.

## ENet Notes

- The plugin currently carries ENet under `Codes/enet`.
- `.gitmodules` also points `Codes/enet` at the upstream ENet repository.
- The plugin CMake uses `FetchContent` with `SOURCE_DIR "${PLUGIN_CODE_DIR}/enet"`.
- If `Codes/enet` is empty in a fresh checkout, initialize submodules before debugging build issues.

## Pre-Submit Checklist

- Build configuration still resolves the correct ToolKit engine path.
- Plugin DLL and `.lib` are produced under `Codes/Bin`.
- No generated artifacts were intentionally edited.
- `Plugin.settings` still describes the correct plugin name and purpose.
- Any public behavior change is reflected in docs or sample integration code.
- If packet formats or replicated state changed, both sending and receiving paths were reviewed together.
