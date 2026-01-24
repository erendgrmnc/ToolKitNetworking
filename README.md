# ToolKit Networking

## Overview
**ToolKitNetworking** is a high-performance, server-authoritative networking middleware designed specifically for the ToolKit Game Engine. It seamlessly integrates with the engine's Entity-Component-System (ECS) to provide robust state replication, RPC capabilities, and efficient bandwidth usage.

This plugin serves as the core networking layer, handling low-level packet management (via ENet) while exposing a clean, high-level API for game logic.

## Technical Features

### 1. Server-Authoritative Replication
*   **State History & Interpolation:** Maintains a history of state snapshots to interpolate entity transforms on clients, ensuring smooth movement even with network jitter.
*   **Delta Compression:** Reduces bandwidth by calculating the difference between the current state and a known baseline state, transmitting only modified properties.
*   **Snapshot Acknowledgment:** Implements a reliability layer on top of UDP to track which snapshots clients have received.

### 2. High-Performance RPC System
*   **Template-Based Dispatch:** Uses C++ variadic templates to serialize and deserialize arbitrary function arguments without runtime reflection overhead.
*   **Zero-Boilerplate Macros:** Simple macros (`TK_RPC_SERVER`, `TK_RPC_MULTICAST`) handle registration and routing automatically.
*   **Cross-Boundary Linking:** Features a global registry to safely map function hashes across DLL boundaries (Game DLL <-> Plugin DLL).

### 3. Smart Network Variables
*   **`NetworkVariable<T>`:** A wrapper class that tracks state changes ("dirty" flags).
*   **Bit-Masked Serialization:** Optimized `PacketStream` serialization uses bit-masks to replicate only dirty variables, minimizing packet size.

### 4. Engine Integration
*   **ECS Hooks:** `NetworkManager` and `NetworkComponent` integrate directly into the ToolKit update loop.
*   **Editor Support:** Custom metadata allows network components to be configured and managed directly within the ToolKit Editor.
*   **DLL Export Management:** Carefully managed symbol exports (`TK_NET_API`) ensure proper linking for both the Editor and standalone game builds.

## Architecture

*   **`NetworkManager`**: The central hub. Manages ENet host/peer lifecycles, processes incoming packets, and orchestrates the tick update loop.
*   **`NetworkComponent`**: The base class for networked entities. Handles property serialization (Position, Rotation) and RPC routing.
*   **`PacketStream`**: A custom binary stream writer/reader optimized for game data.
*   **`NetworkRPCRegistry`**: A static singleton that maps function name hashes to their handler delegates.

## Build Requirements
*   **CMake:** 3.6+
*   **Compiler:** MSVC (Windows) or Clang (Android/Linux)
*   **Dependencies:** ToolKit Engine, ENet (included)
