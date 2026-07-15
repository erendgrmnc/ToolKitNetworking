# Production Networking Implementation Plan

## Goal

Turn `ToolKitNetworking` from a localhost-oriented prototype into a production-ready plugin that supports:

- `DedicatedServer`
- `ListenServer` (player-hosted session)
- `Client`

This plan explicitly does **not** include true mesh P2P. Both dedicated and listen-server modes remain server-authoritative and share the same replication model.

## Current Status

Checkpoint as of April 5, 2026:

- Completed:
  - Phase 1: core session types
  - Phase 2: session manager extraction
  - Phase 3: transport abstraction
  - Phase 4: replication manager extraction
  - Phase 5: handshake and session trust basics
  - Phase 6: endpoint management hardening
  - Phase 7: bootstrap provider layer
- Current production state:
  - direct dedicated join is supported through the provider seam
  - direct listen-server join is supported through the provider seam
  - `SessionDirectory` now has:
    - an explicit process-local fake directory for tests and local-only injection
    - a runtime broker-backed service path with broker config validation and WinHTTP transport composition
  - explicit broker/session-directory DTOs and broker-client adapter mapping now exist behind `ISessionDirectoryService`
  - a transport-backed remote broker client seam now exists for register/lookup request serialization and response parsing
  - `NetworkManager` now surfaces `JoinMethod` plus broker URL/auth-token/timeout settings and builds the runtime session-directory service through the session runtime seam
  - `LanDiscovery` and `BrokeredHostedSession` still exist only as explicit unsupported stubs
  - handshake/auth gating exists before replication traffic
  - endpoint semantics are explicit:
    bind, advertised, join target, resolved transport
- Recommended next step:
  - harden the new runtime broker path with production broker auth/trust expectations and richer route metadata before starting relay work
- Immediate priorities:
  - production broker authentication and trust policy
  - real broker backend compatibility testing beyond fake/local services
  - credential and route expiry semantics end to end
  - relay-compatible route metadata without enabling relay execution yet

## Non-Negotiable Design Rules

1. Keep one replication model.
   `NetworkComponent`, snapshots, ownership, RPC dispatch, and spawn/despawn remain topology-agnostic.

2. Split session/bootstrap from replication.
   The code that decides how a player hosts, joins, discovers, or reconnects must not live inside the gameplay replication loop.

3. Treat listen server as composition, not a special networking model.
   A hosted session is one local authority instance plus one local client connection using the same session and replication abstractions as remote clients.

4. Do not infer public network reachability from local adapter enumeration.
   Local bind endpoint, advertised endpoint, and resolved join target must be separate concepts.

5. Add production-state surfaces before expanding internet behavior.
   No further topology work should rely on ad hoc `TK_LOG` strings and implicit state.

## Current Problems To Solve

- `NetworkManager` mixes session startup, transport lifecycle, editor-facing configuration, and gameplay replication.
- The plugin only supports direct ENet bind/connect flows.
- Endpoint semantics are incomplete: current code effectively has only `host + port`, plus a best-effort local IPv4 helper.
- Listen-server mode is implemented as "start server and then start client" without a real session abstraction.
- Connection lifecycle is not production-safe: timeouts, failure reasons, reconnect, and disconnect states are not properly surfaced.
- There is no protocol/version handshake, session token model, or trustworthy bootstrap story for non-localhost use.

## Target Architecture

### Layer 1: Session / Bootstrap

Add a new session-management layer that owns:

- hosting mode selection
- config precedence
- endpoint resolution
- join and host flows
- connection state machine
- bootstrap provider selection
- public session metadata

Primary types to add:

- `enum class HostingMode { None, Client, DedicatedServer, ListenServer }`
- `enum class JoinMethod { DirectAddress, SessionDirectory, LanDiscovery, BrokeredHostedSession }`
- `enum class ConnectionState { Idle, StartingHost, Discovering, Resolving, Connecting, Handshaking, Connected, Disconnecting, Disconnected, Failed, Reconnecting }`
- `enum class DisconnectReason { None, UserRequested, Timeout, TransportError, VersionMismatch, AuthRejected, ServerShutdown, SessionClosed, ProtocolError }`
- `struct NetworkEndpoint`
- `struct SessionDescriptor`
- `struct SessionJoinRequest`
- `struct SessionHostRequest`
- `struct ConnectionStatus`

Recommended owning class:

- `NetworkSessionManager`

### Layer 2: Transport

Wrap ENet behind transport abstractions so replication does not care whether the connection came from:

- direct dedicated-server join
- direct listen-server join
- future brokered/relay-assisted hosted session

Recommended abstractions:

- `ITransportHost`
- `ITransportPeer`
- `TransportPacketEnvelope`
- `TransportPeerId`

Phase 1 implementation still uses ENet underneath, but through the abstraction boundary.

### Layer 3: Replication

Refactor current replication responsibilities out of `NetworkManager` into a dedicated replication authority/runtime layer.

Recommended owning class:

- `ReplicationManager`

It should own:

- registered `NetworkComponent` objects
- snapshot send/receive
- snapshot ack tracking
- RPC routing
- network object spawn/despawn
- owner assignment
- local-authority and remote-proxy update rules

`NetworkComponent` remains the gameplay-facing extension point.

## Public API and Config Changes

### Replace Current Role Surface

Current `NetworkRole` is not sufficient. Replace or supersede it with:

- `HostingMode`
- editor-visible connection/session settings

`NetworkManager` must remain the public ToolKit-facing and editor-visible component/facade during migration.

Rules:

- existing scenes should continue to serialize a single `NetworkManager` component
- new session, transport, and replication managers should be internal helpers/services unless there is an explicit later migration
- gameplay/sample code should not need to instantiate multiple new top-level networking manager components
- the existing `Role` field must map cleanly into the new session model during transition

### Editor-Visible Parameters To Add

Expose these through ToolKit parameters on the session-facing manager component:

- `HostingMode`
- `ConnectHost`
- `ConnectPort`
- `ListenPort`
- `BindAddress`
- `AdvertisedAddress`
- `MaxClients`
- `JoinMethod`
- `SessionId`
- `EnableLanDiscovery`
- `AllowDirectHostedConnections`
- `EnableRelayFallback`
- `ConnectionTimeoutMs`
- `HandshakeTimeoutMs`
- `AutoReconnect`
- `BuildCompatibilityId`

### CLI Override Surface

CLI overrides should not be the primary configuration source anymore. They should override editor/runtime config.

Add or normalize support for:

- `-mode=client|dedicated|listen`
- `-connectHost=<host>`
- `-connectPort=<port>`
- `-listenPort=<port>`
- `-bindAddress=<ip>`
- `-advertisedAddress=<host-or-ip>`
- `-sessionId=<id>`
- `-joinMethod=direct|directory|lan|brokered`

### Config Precedence

Lock precedence as:

1. explicit runtime bootstrap result
2. CLI override
3. editor-saved component settings
4. plugin default fallback

## Step-By-Step Implementation

### Phase 1: Introduce Core Session Types

1. Add the new enums and config structs in new shared networking headers.
2. Add `NetworkEndpoint`, `SessionDescriptor`, `SessionHostRequest`, `SessionJoinRequest`, `ConnectionStatus`, and disconnect/failure reason types.
3. Add protocol/build compatibility constants and a session protocol version identifier.
4. Keep the types independent of ENet.

Done criteria:

- Session and endpoint concepts compile without changing replication behavior.
- No gameplay code depends on raw `host + port` directly anymore once the refactor is complete.

### Phase 2: Extract Session Management From `NetworkManager`

1. Create `NetworkSessionManager`.
2. Move startup role selection, host/join decisions, config precedence, and lifecycle state into it.
3. Keep `NetworkManager` as the public ToolKit component/facade and have it own or delegate to `NetworkSessionManager`.
4. Keep `PluginMain` responsible only for locating the public manager component and forwarding play/stop events.
5. Replace direct `StartAsServer` / `StartAsClient` orchestration from gameplay entry code with session-level `HostSession(...)`, `JoinSession(...)`, and `ShutdownSession()`.

Done criteria:

- `PluginMain` no longer contains topology decisions beyond invoking the public manager facade.
- Connection state is queryable without inspecting internal transport objects.
- Existing scene/editor usage of `NetworkManager` remains valid.

### Phase 3: Introduce Transport Abstractions

1. Add `ITransportHost` and `ITransportPeer`.
2. Add ENet-backed implementations that preserve current packet behavior.
3. Move connect/disconnect/event-service logic out of `GameClient` / `GameServer` into those transport-backed implementations or adapt those classes to the interface.
4. Normalize peer/channel handling and eliminate hardcoded assumptions like `maxClients = 2`.

Done criteria:

- The session layer talks to transport interfaces, not directly to raw ENet objects.
- Dedicated and listen-server flows use the same transport contracts.

### Phase 4: Extract Replication Responsibilities

1. Create `ReplicationManager`.
2. Move snapshot send/receive, ack handling, spawn/despawn replication, owner assignment, and RPC forwarding out of `NetworkManager`.
3. Keep `NetworkComponent` integration unchanged from a gameplay author's perspective where possible.
4. Replace direct `NetworkComponent -> NetworkManager::Instance` coupling with a stable facade or service access pattern for ownership, tick access, unregistering, and RPC send paths.
5. Ensure hosted sessions and dedicated sessions both call the same replication entry points.

Done criteria:

- Replication logic has no knowledge of how the session was discovered or joined.
- Session/bootstrap changes no longer require edits in snapshot/RPC code paths.
- `NetworkComponent` no longer depends on old singleton assumptions leaking through the new architecture.

### Phase 5: Add Handshake and Session Trust Basics

1. Add a handshake packet flow before replication starts.
2. Validate:
   - protocol version
   - build compatibility id
   - public session id when routing/discovery requires it
   - opaque join credential or auth token
   - hosting mode compatibility
3. Reject incompatible or malformed joins with explicit reasons.
4. Ensure all authoritative packet handling requires a validated authenticated session.
5. Add nonce/challenge protection or equivalent replay resistance for handshake establishment.

Done criteria:

- A client cannot enter active replication without successful handshake completion.
- Failure reasons are surfaced in structured state, not only logs.
- Session identity and authentication credential are distinct concepts in code and protocol.

### Phase 6: Harden Endpoint Management

1. Separate:
   - local bind endpoint
   - advertised/public endpoint
   - join target
   - resolved transport endpoint
2. Remove any product behavior that depends on `GetIPV4()` as a public-address answer.
3. Keep `GetIPV4()` only as a debugging utility if it remains at all.
4. Add support for explicit advertised endpoint configuration on listen-server and dedicated modes.

Done criteria:

- Multi-homed hosts, VPN users, and dedicated deployments can specify correct endpoints without code changes.

### Phase 7: Add Bootstrap Provider Layer

1. Add a bootstrap/provider interface with at least these implementations:
   - direct address join
   - dedicated session directory stub/provider
   - LAN discovery stub/provider
   - brokered hosted-session stub/provider
2. Lock v1 supported production paths to:
   - direct dedicated join
   - direct listen-server join
3. Keep session directory, LAN discovery, and brokered hosted-session providers as extension points or stubs unless they are fully implemented in the same milestone.
4. The plugin must be able to host/join dedicated and listen-server sessions through the same session manager surface.

Done criteria:

- New discovery or relay strategies can be added without rewriting replication code.
- v1 release scope is explicit and does not imply unsupported bootstrap paths.

Implementation status:

- Completed.
- `DirectAddress` is implemented as the real v1 bootstrap provider.
- `SessionDirectory`, `LanDiscovery`, and `BrokeredHostedSession` are explicit unsupported providers that fail with `DisconnectReason::BootstrapFailed`.
- `NetworkSessionManager` now resolves host/join metadata through the provider seam before transport startup.

### Phase 8: Observability and Failure Handling

1. Add structured connection/session logging.
2. Add connection state transitions with reasons.
3. Convert frame-count timeout logic to elapsed-time logic.
4. Handle connect failure, disconnect, timeout, shutdown, and reconnect consistently.
5. Add per-peer and per-session metrics:
   - handshake outcome counts
   - disconnect reason counts
   - timeout counts
   - active connection counts
   - dedicated-server health/heartbeat state
6. Add minimal debug state surfaces accessible from the manager component or plugin diagnostics.

Done criteria:

- Dedicated and listen-server failures are diagnosable without log archaeology.
- Timeouts and disconnects produce deterministic state transitions.
- Minimal operational telemetry exists for both local debugging and dedicated-server support.

Implementation status:

- Not started as a formal phase.
- Some foundations already exist from earlier work:
  - elapsed-time connection and handshake timeouts
  - explicit connection state tracking
  - bootstrap failure reasons
- The next implementation checkpoint should be a security-hardening pass immediately before or alongside early Phase 8 diagnostics work.

### Phase 9: Editor Integration and UX Cleanup

1. Expose new config fields through ToolKit parameters.
2. Keep defaults safe for local development.
3. Ensure hosted and dedicated flows can be launched from editor config plus CLI overrides.
4. Keep localhost development convenient, but clearly mark it as a development default rather than the only path.

Done criteria:

- The editor configuration is sufficient to host or join non-localhost sessions without code edits.

### Phase 10: Compatibility and Migration Cleanup

1. Preserve the existing localhost direct-connect workflow as a fallback/dev mode.
2. Preserve `NetworkManager` as the serialized/editor-visible facade while internal layers are extracted.
3. Define migration from old `NetworkRole` values to the new `HostingMode` and session configuration surface.
4. Remove or deprecate old startup paths once the session manager fully owns them.
5. Update docs after implementation:
   - architecture overview
   - build/runtime instructions
   - contribution guidance

Done criteria:

- Existing project users can still run local sessions during migration.
- The final public surface is coherent and no longer split across legacy and new startup paths.
- Existing saved scenes/components remain loadable without manual repair.

## File-Level Implementation Map

### Likely New Files

- `Codes/NetworkSessionTypes.h`
- `Codes/NetworkSessionManager.h`
- `Codes/NetworkSessionManager.cpp`
- `Codes/ReplicationManager.h`
- `Codes/ReplicationManager.cpp`
- `Codes/ITransportHost.h`
- `Codes/ITransportPeer.h`
- `Codes/EnetTransportHost.h`
- `Codes/EnetTransportHost.cpp`
- `Codes/EnetTransportPeer.h`
- `Codes/EnetTransportPeer.cpp`
- `Codes/SessionBootstrapProvider.h`
- `Codes/DirectJoinProvider.h`
- `Codes/DirectJoinProvider.cpp`

### Existing Files Expected To Change

- `Codes/NetworkManager.h`
- `Codes/NetworkManager.cpp`
- `Codes/GameClient.h`
- `Codes/GameClient.cpp`
- `Codes/GameServer.h`
- `Codes/GameServer.cpp`
- `Codes/NetworkPackets.h`
- `Codes/PluginMain.cpp`
- `Codes/CMakeLists.txt`

## Packet / Protocol Work

Add explicit packets or message types for:

- client hello / join request
- server accept / reject
- compatibility mismatch
- session metadata
- disconnect reason

Rules:

- handshake packets must complete before snapshot/RPC traffic is accepted
- authoritative state updates must be ignored from unvalidated peers
- protocol versioning must be explicit in the handshake

## Security Requirements

- Treat `SessionId` as a public routing/discovery identifier, not an authentication secret.
- Require an opaque join credential or token for any non-open join flow.
- Define credential issuer, lifetime, scope, expiry, and rejection behavior before implementation starts.
- Restrict pre-auth traffic to a strict allowlist:
  handshake, compatibility negotiation, and disconnect/close messages only.
- Reject or disconnect on any pre-auth `RPC`, `ClientUpdate`, `Spawn`, `Despawn`, or oversized packet.
- Enforce packet size ceilings and bounds validation before deserialization.
- Enforce malformed-packet disconnect policy and log the reason without leaking sensitive credentials.
- Add per-peer rate limits, max pending handshakes, reconnect backoff, and penalties for repeated invalid handshakes.
- Require replay and downgrade resistance in the handshake path.
- Redact join credentials, relay credentials, and other secrets from logs and diagnostics.
- Define privacy rules for bootstrap providers:
  whether host IPs are exposed, whether public endpoints are directory-visible, and how broker/relay credentials are protected.
- Default publicly reachable hosted sessions to secure settings rather than open insecure joins.

## Testing Requirements

### Automated

- handshake success and reject cases
- invalid protocol/build compatibility
- malformed and oversized packet rejection
- unknown message type handling
- replay and duplicate handshake attempts
- join-token misuse and expired credential cases
- handshake spam and connection flood behavior
- downgrade and mismatched feature-set tests
- connection timeout and disconnect transitions
- snapshot ack behavior after handshake
- ownership enforcement for client updates
- RPC rejection from invalid or wrong-owner peers
- rate-limit enforcement
- endpoint parsing and precedence logic

### Manual

- localhost direct-connect regression
- same-LAN two-machine dedicated join
- same-LAN listen-server join
- remote dedicated join using a known reachable endpoint
- multi-homed host with explicit bind and advertised endpoints
- session privacy and endpoint exposure checks
- shutdown and reconnect flows

### Acceptance Criteria

- same replication code works for dedicated and listen-server modes
- no gameplay component code needs to know whether authority is dedicated or hosted
- connection state is explicit and queryable
- non-localhost endpoints can be configured without source edits
- localhost behavior still works as a development path

## Agent Directives

Use these directives if multiple coding agents implement the plan in parallel.

### Global Rules

- Do not edit the engine repo at `C:/Users/erendegirmenci/Desktop/Projects/GDTK` unless explicitly requested.
- Do not change replication behavior and session/bootstrap behavior in the same patch unless the interface contract forces it.
- Do not commit generated artifacts under `Codes/Bin`, `Codes/Intermediate`, `Intermediate`, `build`, or `x64`.
- Preserve the current local-development flow until the replacement path is verified.
- Do not introduce true mesh P2P authority.
- Follow existing ToolKit code conventions:
  Allman braces, `TKDeclareClass`, `TKDeclareParam`, existing `Ptr` typedef patterns, and the current parameter/metadata system.
- Do not introduce a parallel generic config framework when ToolKit-native parameter/config surfaces can be used.

### Agent Slice A: Session / Config / Lifecycle

Own:

- new session types
- session manager
- hosting mode config
- CLI/config precedence
- plugin lifecycle integration

Primary files:

- `Codes/NetworkSessionTypes.h`
- `Codes/NetworkSessionManager.*`
- `Codes/PluginMain.cpp`
- `Codes/NetworkManager.*`

Do not own:

- snapshot serialization format
- low-level ENet event loop internals

### Agent Slice B: Transport Abstraction

Own:

- transport interfaces
- ENet-backed transport implementations
- connection state transitions
- timeout/disconnect/failure handling

Primary files:

- `Codes/ITransportHost.h`
- `Codes/ITransportPeer.h`
- `Codes/EnetTransportHost.*`
- `Codes/EnetTransportPeer.*`
- `Codes/GameClient.*`
- `Codes/GameServer.*`

Do not own:

- gameplay replication semantics
- editor parameter definitions

### Agent Slice C: Replication Refactor

Own:

- replication manager extraction
- snapshot send/receive
- ack handling
- spawn/despawn replication
- RPC routing integration

Primary files:

- `Codes/ReplicationManager.*`
- `Codes/NetworkComponent.*`
- `Codes/NetworkPackets.h`

Do not own:

- session discovery UX
- bootstrap provider policy
- public `NetworkManager` facade ownership

### Agent Slice D: Bootstrap Providers

Own:

- direct join provider
- bootstrap provider interface
- LAN/discovery or brokered provider stubs
- session descriptor resolution

Primary files:

- `Codes/SessionBootstrapProvider.*`
- `Codes/DirectJoinProvider.*`
- provider-specific files added later

Do not own:

- snapshot/RPC internals
- ToolKit component metadata

### Integration Rules

- `NetworkManager` is the highest-conflict file; coordinate interface changes before editing it.
- Land a minimal `NetworkManager` facade/seam split first, then let session and replication slices proceed behind that seam.
- Keep write ownership disjoint wherever possible.
- When an interface changes, update the plan section in this file and document the exact contract.
- Do not merge provider-specific shortcuts into replication code.

## Recommended Delivery Order

1. Session types and state machine
2. Transport abstraction
3. Session manager extraction
4. Replication manager extraction
5. Handshake and trust model
6. Endpoint separation
7. Direct join provider
8. Listen-server composition polish
9. Observability and failure handling
10. Documentation and migration cleanup

## Final Success Criteria

The work is complete when:

- one plugin supports `DedicatedServer`, `ListenServer`, and `Client`
- both dedicated and listen-server use the same server-authoritative replication path
- session/bootstrap behavior is isolated from gameplay replication
- endpoint semantics are explicit and production-safe
- connection failures and compatibility mismatches are surfaced cleanly
- the plugin is ready for future LAN discovery, brokered join, and relay fallback without architectural rewrite
