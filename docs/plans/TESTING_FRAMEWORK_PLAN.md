# ToolKitNetworking Testing Framework Plan

## Goal

Add a production-grade testing framework to `ToolKitNetworking` that supports:

- fast unit tests
- regression tests
- deterministic integration tests
- narrow real-transport smoke tests
- adversarial security tests

The framework must fit the current refactor state of the plugin and expand with the networking architecture instead of locking in temporary seams.

## Current Status

Checkpoint as of April 5, 2026:

- Completed:
  - opt-in plugin-local test build
  - `CTest + GoogleTest` integration
  - isolated test output under `Plugins/ToolKitNetworking/Intermediate/Tests`
  - `ToolKitNetworkingCore`
  - `ToolKitNetworkingSessionCore`
  - deterministic `ManualClock` support
  - unit and integration test targets
- Current covered areas:
  - session type defaults and role mapping
  - command-line override parsing
  - endpoint precedence and split connect/listen overrides
  - session manager state transitions
  - connection and handshake timeout behavior
  - handshake success/reject flows
  - bootstrap provider resolution and unsupported-provider failure
  - handshake helper coverage for fixed-size packet validation, duplicate/stale detection, and rate-limit windows
  - engine-coupled fake transport coverage for replication pre-auth rejection and handshake abuse controls
- Still missing:
  - dedicated security harness fixtures
  - broader replay, malformed-packet, rate-limit, and secret-redaction tests
  - observability/diagnostics assertions
- Recommended next testing step:
  - keep expanding engine-coupled replication security coverage in parallel with the security-hardening pass

## Review Outcome

Parallel review across logic integrity, industry-standard practice, project fit, and security produced one consistent conclusion:

- implement the testing framework now
- do not try to fully automate every final networking behavior yet
- stage coverage based on architecture maturity
- keep early tests engine-light and deterministic
- reserve real ENet and editor-coupled tests for focused smoke/integration coverage

## Non-Negotiable Rules

1. Do not place test outputs in `Plugins/ToolKitNetworking/Codes/Bin`.
2. Do not attach tests to the main plugin DLL target.
3. Unit tests must not require ToolKit editor/plugin loading unless there is no clean alternative.
4. Test seams must improve architecture, not preserve temporary coupling.
5. Security-sensitive fixtures must use synthetic credentials only and must never log secrets.

## Current Constraints

- There is no plugin-local test target today.
- The current plugin build is a ToolKit/editor-coupled DLL build under `Plugins/ToolKitNetworking/Codes/CMakeLists.txt`.
- `NetworkSessionManager` and transport code are still partially coupled to runtime/plugin behavior.
- Handshake, auth gating, endpoint hardening, bootstrap providers, and observability are not complete yet.
- `NetworkComponent -> NetworkManager::Instance` coupling still limits replication test ergonomics.

## Recommended Stack

- `CTest` as the test runner integration
- `GoogleTest` as the assertion/test framework
- opt-in CMake switches for test builds
- a new pure static library target for testable networking core logic

Rationale:

- `CTest + GoogleTest` is standard, practical on Windows, and fits the current CMake-based build.
- The plugin needs a split between pure logic tests and ToolKit/ENet-coupled tests.
- A core static library is the cleanest way to keep session, handshake, endpoint, and packet logic testable without editor/plugin startup.

## Target Layout

Create this structure under the plugin root:

```text
Plugins/ToolKitNetworking/
  Tests/
    CMakeLists.txt
    Unit/
    Regression/
    Integration/
    Smoke/
    Support/
```

Recommended support files:

- `Tests/Support/FakeTransportHost.*`
- `Tests/Support/FakeTransportPeer.*`
- `Tests/Support/ManualClock.*`
- `Tests/Support/TestPacketFactory.*`
- `Tests/Support/TestSessionBuilder.*`
- `Tests/Support/RedactedLogCapture.*`

## CMake Plan

## Step 1: Add Opt-In Test Build

In `Plugins/ToolKitNetworking/Codes/CMakeLists.txt`:

- add `include(CTest)`
- add `option(TK_NET_BUILD_TESTS OFF)`
- add `option(TK_NET_BUILD_ENGINE_TESTS OFF)`
- add `option(TK_NET_BUILD_ENET_SMOKE_TESTS OFF)`
- add `add_subdirectory("../Tests" "../Intermediate/Tests")` only when `TK_NET_BUILD_TESTS` is enabled

Rules:

- test executables and support libraries must output to `Plugins/ToolKitNetworking/Intermediate/Tests/<Config>`
- do not reuse plugin hot-reload output rules
- do not mix test binaries with plugin packaging/runtime binaries

## Step 2: Add Test Dependency Ownership

In `Plugins/ToolKitNetworking/Tests/CMakeLists.txt`:

- `enable_testing()`
- fetch `GoogleTest` as a test-only dependency owned by the plugin test build
- define test labels:
  - `unit`
  - `regression`
  - `integration`
  - `security`
  - `enet_smoke`

Do not rely on a transitive copy of a test framework from engine dependencies.

## Code Architecture Plan

## Step 3: Carve Out `ToolKitNetworkingCore`

Create a pure static library for logic that should be testable without ToolKit editor/plugin startup.

Recommended target:

- `ToolKitNetworkingCore`

Move or isolate logic here as phases progress:

- endpoint parsing and precedence
- session request building
- connection state machine logic
- handshake state and validation
- packet codec/bounds logic
- disconnect reason mapping
- snapshot ack bookkeeping
- ownership and policy checks that do not require live scene objects

Keep this target:

- C++17
- engine-light
- independent of plugin DLL loading
- independent of ToolKit editor UI and `PluginMain`

## Step 4: Make Transport Testing Deterministic

The testing framework must not depend only on real ENet sockets.

Add test support primitives:

- `ManualClock`
  controls elapsed time explicitly for timeout/backoff/reconnect tests
- `FakeTransportHost`
- `FakeTransportPeer`
- scripted transport behavior:
  - delay
  - drop
  - duplicate
  - reorder
  - forced disconnect
  - reason injection

Follow-up architecture rule:

- future transport contracts should become byte/event oriented instead of exposing raw `GamePacket*` and `PacketReceiver` to upper layers

## Step 5: Add a Security Test Harness

Add deterministic hooks for adversarial cases:

- raw pre-auth packet injection
- malformed packet generation
- replay of captured handshake messages
- downgrade and compatibility mutation
- rate-limit and flood simulation
- disconnect reason capture
- redacted log capture

This harness must support:

- pre-auth allowlist validation
- post-auth boundary checks
- credential separation:
  public `SessionId` vs opaque join credential

## Coverage Plan By Phase

## Step 6: Implement Immediate Coverage

Write tests now for behavior that already exists or is stable enough to lock down.

### Unit

- `NetworkRole -> HostingMode` mapping
- command-line override parsing
- host request construction
- join request construction
- connection status transitions up to raw transport connect
- current direct join defaults
- transport interface contracts

### Regression

- packet struct size/field sanity where stable
- snapshot ack bookkeeping
- owner enforcement for `ClientUpdate`
- listen-server startup ordering
- stop/shutdown idempotence

### Integration

- session manager using fake transport:
  - dedicated host startup
  - client join startup
  - listen-server startup
  - failed connect propagation

Do not add broad ENet-heavy end-to-end coverage yet.

## Step 7: Add Replication Tests After Replication Extraction

After Phase 4 of the production networking plan:

- add `ReplicationManager` contract tests
- test snapshot send/receive policy
- test spawn/despawn dispatch
- test RPC routing
- test ownership enforcement
- test ack handling

Goal:

- replication tests should not care how the session was discovered or joined

## Step 8: Add Handshake and Security Tests After Handshake Exists

After Phase 5:

- handshake success
- protocol mismatch rejection
- build compatibility rejection
- credential rejection
- malformed hello/challenge/response packets
- replayed handshake packets
- downgrade attempts
- pre-auth `RPC`, `ClientUpdate`, `Spawn`, `Despawn` rejection
- auth gate before replication activation

Implementation status:

- Partially completed.
- Handshake success/reject and auth-gating-adjacent session tests exist.
- The remaining gap is adversarial coverage:
  malformed bootstrap/handshake payloads, replay attempts, rate limits, and secret redaction.

## Step 9: Add Endpoint and Bootstrap Tests After Those Layers Exist

After Phases 6 and 7:

- bind endpoint vs advertised endpoint vs join target separation
- multi-homed explicit endpoint config
- stale/tampered session descriptor rejection
- direct join provider resolution
- LAN/discovery/brokered provider stubs behaving predictably

Implementation status:

- Partially completed.
- Existing tests now cover:
  - bind vs advertised vs join-target precedence
  - direct join provider resolution
  - unsupported provider failure
  - custom provider rewrite of resolved join target/session metadata
- Still missing:
  - stale or tampered session descriptor rejection
  - LAN/discovery/brokered provider contract tests beyond predictable stub failure

## Step 10: Add Observability and Failure-Path Tests

After Phase 8:

- elapsed-time timeout transitions
- structured disconnect reasons
- reconnect backoff behavior
- handshake flood accounting
- active connection counters
- metric increments for reject/timeout/disconnect cases
- secret redaction in logs and diagnostics

## Test Categories

## Unit

Purpose:

- fast, deterministic, no real sockets, no editor/plugin startup

Targets:

- session types
- connection state machine logic
- endpoint/config parsing
- handshake validators
- packet codec bounds validation

## Regression

Purpose:

- lock down bug fixes and protocol invariants

Examples:

- specific malformed packet crashes
- previous connect/disconnect regressions
- stale ack handling
- wrong-owner update rejection

## Integration

Purpose:

- verify multiple internal layers work together with fakes

Examples:

- session manager + fake transport
- session manager + replication manager
- handshake + replication gating

## Security

Purpose:

- adversarial behavior, malformed input, abuse resistance

Required matrices:

- malformed packet matrix
- replay/downgrade matrix
- rate-limit/flood matrix
- secret-handling matrix

## ENet Smoke

Purpose:

- minimal real transport verification only

Examples:

- loopback dedicated join
- loopback listen-server join
- connect, replicate basic traffic, disconnect

These should remain narrow and Windows-oriented.

## Security-Specific Requirements

The testing plan must explicitly validate:

- no unauthenticated packet reaches replication handlers
- malformed packets are bounded and rejected safely
- replay and downgrade attempts fail deterministically
- rate limits trigger without leaking memory or state
- logs never expose join credentials or relay credentials
- tests do not store reusable secrets in fixtures or golden files

Required adversarial cases:

- truncated headers
- oversized declared lengths
- unknown message types
- invalid enum values
- corrupted handshake fields
- duplicate/non-monotonic nonce or challenge data
- token reuse across session ids
- stale/expired join credentials
- handshake spam
- reconnect storms

## CI and Execution Strategy

Recommended CI split:

- per PR:
  - `unit`
  - `regression`
  - `integration`
- Windows smoke lane:
  - `enet_smoke`
- manual release gates:
  - same-LAN dedicated join
  - same-LAN listen-server join
  - multi-homed explicit endpoint test
  - remote dedicated join

Do not make two-machine or WAN-style tests mandatory for every PR.

## Agent Directives

If multiple agents implement this plan in parallel, use these ownership slices.

### Slice A: Test Build Infrastructure

Own:

- `Tests/CMakeLists.txt`
- root test options
- `CTest` integration
- output directories

Do not own:

- handshake semantics
- replication assertions

### Slice B: Core Testability Refactor

Own:

- `ToolKitNetworkingCore`
- logic extraction for unit tests
- manual clock seams
- deterministic helpers

Do not own:

- plugin/editor UI surfaces

### Slice C: Fake Transport and Integration Support

Own:

- fake host/peer
- scripted transport behavior
- integration fixtures

Do not own:

- real ENet smoke flows

### Slice D: Security Harness

Own:

- malformed packet generator
- replay/downgrade fixtures
- flood/rate-limit fixtures
- log redaction assertions

Do not own:

- production credential issuance policy

## Acceptance Criteria

The testing framework work is complete when:

- the plugin has an opt-in test build path
- unit tests run without polluting plugin runtime bins
- early tests cover current stable session/transport logic
- replication tests become possible without transport discovery coupling
- handshake/security behavior can be tested deterministically with fakes
- ENet smoke tests remain narrow and reliable
- the framework can grow with the remaining production networking phases without major rewrite

## Recommended Delivery Order

1. Add opt-in test CMake and test directory layout
2. Add `GoogleTest + CTest`
3. Create `ToolKitNetworkingCore`
4. Add fake transport and manual clock support
5. Add immediate unit/regression tests for current seams
6. Add replication tests after Phase 4
7. Add handshake/security tests after Phase 5
8. Add endpoint/bootstrap tests after Phases 6 and 7
9. Add observability/failure-path tests after Phase 8
10. Add narrow ENet smoke tests and CI wiring
