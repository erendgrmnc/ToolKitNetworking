# ToolKitNetworking Security And Endpoint Roadmap

## Scope
This document defines how `ToolKitNetworking` should handle real network endpoints and internet-facing session security for the supported production topologies:

- `DedicatedServer`
- `ListenServer`
- `Client`

This roadmap does not target true mesh peer-to-peer networking. Gameplay remains server-authoritative in both dedicated and listen-server modes.

## Current Checkpoint

Checkpoint as of April 5, 2026:

- Endpoint separation work is complete enough for direct dedicated and direct listen-server flows.
- The bootstrap provider seam is in place.
- Supported v1 bootstrap path:
  - `DirectAddress`
- Supported non-production broker/discovery path:
  - `SessionDirectory` via explicitly injected process-local fake broker registration and lookup
- Supported broker contract work:
  - broker/session-directory request and response DTOs
  - broker-client-to-session-directory error mapping behind `ISessionDirectoryService`
  - transport-backed remote broker client request serialization and response parsing
  - concrete Windows-first WinHTTP transport for runtime broker-backed `SessionDirectory`
- Explicit unsupported stubs still exist for:
  - `LanDiscovery`
  - `BrokeredHostedSession`
- Handshake/auth gating exists before gameplay replication traffic.

Current active step:

- harden the new runtime broker-backed `SessionDirectory` path and validate it against a real broker contract before relay implementation

## Core Endpoint Model
The plugin must keep three distinct endpoint concepts:

1. `BindEndpoint`
- The local interface and port the server socket binds to.
- Examples:
  - `0.0.0.0:7777`
  - `192.168.1.20:7777`

2. `AdvertisedEndpoint`
- The routable address and port presented to remote clients or a broker.
- Examples:
  - `game.example.com:7777`
  - `203.0.113.10:7777`

3. `JoinTarget`
- The actual endpoint a client dials after bootstrap resolution.
- It may equal the advertised endpoint, or a broker/relay-provided endpoint.

These values must never be treated as interchangeable.

## Real-World Examples
### Dedicated Server
- Bind: `0.0.0.0:7777`
- Advertised: `game.example.com:7777`
- Join target: `game.example.com:7777`

### Listen Server On LAN
- Bind: `192.168.1.20:7777`
- Advertised: `192.168.1.20:7777`
- Join target: `192.168.1.20:7777`

### Listen Server Behind NAT With Port Forwarding
- Bind: `0.0.0.0:7777`
- Advertised: `<public-ip-or-dns>:7777`
- Join target: `<public-ip-or-dns>:7777`

### Listen Server Self-Join
- Bind: `0.0.0.0:7777`
- Advertised: `<public-ip-or-dns>:7777`
- Local join target: `127.0.0.1:7777` or LAN IP

## Rules
- `GetIPV4()` must not be treated as a public endpoint discovery mechanism.
- Public endpoint selection must come from explicit config, broker data, or a relay provider.
- Client join flow must consume a resolved session descriptor, not infer public reachability from local interfaces.
- Session/bootstrap code must log bind, advertised, and resolved endpoints separately.

## Security Position
Current handshake/auth work is necessary, but not sufficient for public internet production. Raw ENet transport should be treated as untrusted until the plugin adds authenticated and encrypted session traffic.

For production internet use, the plugin must provide:

- explicit authenticated session establishment
- build/protocol compatibility checks
- replay-resistant handshake flow
- packet bounds validation
- rate limiting and handshake abuse controls
- authenticated gating before gameplay packets
- encrypted or relay-protected transport for exposed sessions

## Certificates And TLS
Do not design the player-hosted path around every listen server having a traditional TLS certificate.

Why:
- ENet is UDP-based, not a TLS-by-default transport.
- Consumer listen servers do not fit certificate lifecycle management well.
- NAT traversal and player-hosted internet sessions usually need broker/relay support anyway.

Practical guidance:
- `DedicatedServer` is the first secure production target.
- `ListenServer` internet play should move toward brokered bootstrap plus relay fallback.
- If transport confidentiality is required before relay support exists, use an authenticated encryption layer appropriate for UDP traffic rather than assuming plain TLS.

## Threat Model Baseline
The plugin should assume attackers can:

- send malformed UDP payloads
- spam handshake attempts
- replay captured handshake or gameplay packets
- connect with mismatched protocol/build versions
- guess object IDs and attempt unauthorized RPCs
- brute force session identifiers if they are weak or enumerable

The plugin should not assume:

- local NIC enumeration gives a public address
- home NATs are configured correctly
- direct hosted sessions are reachable from the internet
- raw transport metadata is trustworthy

## Required Security Controls
### Handshake And Session Trust
- Public `SessionId` must be separate from opaque join credentials.
- Join credentials must be short-lived and treated as secrets.
- Handshake must include:
  - protocol version
  - build compatibility ID
  - session ID when applicable
  - join credential when required
  - server-issued challenge or nonce
- Successful handshake must be required before accepting gameplay packets.

### Replay Resistance
- Add server-issued challenge values to handshake completion.
- Reject stale or duplicate handshake responses.
- Add monotonic or windowed replay checks where packet authenticity is introduced.

### Transport And Packet Safety
- Enforce maximum packet sizes before decode.
- Validate all string and payload lengths before copying or skipping.
- Disconnect or reject on malformed pre-auth traffic.
- Maintain a pre-auth allowlist of packet types.

### Abuse Controls
- Per-peer or per-address handshake rate limiting
- Max pending handshakes
- Connection backoff after repeated failures
- Session close behavior for expired or revoked credentials

### Logging And Secrets
- Never log raw join credentials.
- Redact sensitive session bootstrap material.
- Include stage-based diagnostics:
  - bind success/failure
  - advertised endpoint used
  - resolved join target
  - handshake rejection reason
  - timeout stage

## Endpoint Rollout Plan
### Phase 6
- Explicit bind, advertised, and join-target config
- Max clients and session metadata surfaced through `NetworkManager`
- Direct transport starts from resolved session requests

### Phase 7
- Add bootstrap provider abstraction
- Support:
  - direct dedicated join
  - direct listen-server join
- Keep brokered discovery and relay as extension points

### Phase 8
- Improve diagnostics and failure reporting
- Record which endpoint was bound, advertised, and dialed
- Surface timeout stage and auth rejection details clearly

### Security Hardening Pass
- Add nonce/challenge replay protection
- Add stronger malformed-packet policy
- Add rate limiting and pending-handshake caps
- Add token redaction and structured auth-failure logging

### Brokered Session Discovery
- Introduce a session directory or broker service
- Session broker returns:
  - public `SessionId`
  - opaque join credential
  - resolved join route
  - optional relay assignment

### Relay Support
- Add relay fallback for listen-server sessions
- Prefer relay for user-friendly hosted internet play
- Treat direct hosted joins as an advanced/manual mode unless relay is unavailable

## Recommended Production Order
1. Harden `DedicatedServer` direct-connect first.
2. Support manual `ListenServer` internet play with explicit forwarding expectations.
3. Add brokered session discovery.
4. Add relay fallback.
5. Only then treat cross-network hosted sessions as fully production-ready.

## Acceptance Criteria
This roadmap is satisfied when:

- endpoint semantics are explicit and never conflated
- direct dedicated sessions work with stable public endpoints
- listen-server sessions can distinguish bind vs advertised vs resolved addresses
- unauthenticated gameplay traffic is rejected
- session credentials are opaque and not logged
- the plugin exposes enough diagnostics to explain why a connection failed
- broker and relay can be added without rewriting replication logic

## Agent Directives
When editing endpoint or security code in this plugin:

- do not infer public internet endpoints from local NIC enumeration
- do not reintroduce raw localhost defaults into public runtime paths
- keep `NetworkManager` as the public facade
- keep replication topology-agnostic
- treat `SessionId` as public metadata and join credentials as secrets
- add tests for any new failure path or packet validation rule
- prefer dedicated-server security guarantees first, then hosted-session usability
