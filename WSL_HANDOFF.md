# ToolKitNetworking WSL Handoff

## Purpose
This file is the handoff state for continuing `Plugins/ToolKitNetworking` work from a WSL Ubuntu Codex session.

Use this as the starting point instead of re-scanning the whole plugin.

Canonical docs updated with this same checkpoint:
- [PRODUCTION_NETWORKING_PLAN.md](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/PRODUCTION_NETWORKING_PLAN.md)
- [TESTING_FRAMEWORK_PLAN.md](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/TESTING_FRAMEWORK_PLAN.md)
- [NETWORK_SECURITY_AND_ENDPOINT_ROADMAP.md](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/NETWORK_SECURITY_AND_ENDPOINT_ROADMAP.md)

## Workspace
- Windows workspace root:
  `C:\Users\erendegirmenci\toolkit\eren`
- Engine upstream reference:
  `C:\Users\erendegirmenci\Desktop\Projects\GDTK`
- Plugin root:
  `C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking`

In WSL this will typically be available under:
- `/mnt/c/Users/erendegirmenci/toolkit/eren`
- `/mnt/c/Users/erendegirmenci/Desktop/Projects/GDTK`

## Important Constraint
This plugin is still Windows-first for build verification.

Recommended workflow:
- edit from WSL if preferred
- verify builds and tests on Windows

Do not assume the plugin is ready for native Linux/WSL compilation without additional platform work.

WSL note:
- this WSL session can invoke Windows-side `cmd.exe` / `powershell.exe` build and test commands against the plugin and generated project
- use the documented Windows build/test paths from WSL when needed
- run MSVC-backed targets serially, not in parallel, because shared `.pdb` outputs such as `ToolKitNetworkingCore.pdb` can collide during concurrent builds
- short Windows commands usually work attached from WSL, but long attached plugin builds can intermittently fail with `WSL ... UtilAcceptVsock ... accept4 failed 110`
- for long standalone plugin DLL builds from WSL, prefer detached launch plus log-file and exit-file polling instead of keeping the WSL bridge attached
- repo-local detached helper scripts now live under:
  - `Plugins/ToolKitNetworking/Scripts/run_windows_plugin_build.cmd`
  - `Plugins/ToolKitNetworking/Scripts/run_windows_ctest.cmd`
  - `Plugins/ToolKitNetworking/Scripts/run_windows_project_build.cmd`

## Current Phase Status
Checkpoint as of April 5, 2026:

Completed:
- Phase 1: session types
- Phase 2: session manager extraction
- Phase 3: transport abstraction
- Phase 4: replication manager extraction
- Phase 5: handshake and session trust basics
- Phase 6: endpoint management hardening
- Phase 7: bootstrap provider layer
- Phase 8 is started:
  - diagnostics/security groundwork is in place
  - `SessionDirectory` now has:
    - an explicit process-local fake directory for local/test injection
    - a runtime broker-backed service path behind `INetworkSessionRuntime`
  - explicit broker/session-directory DTOs and a broker-client-backed service adapter now exist behind the session-directory seam
  - a transport-backed remote broker client seam now exists for register/lookup request serialization and response parsing
  - a concrete Windows-first WinHTTP broker transport now exists for runtime `SessionDirectory` use

Testing framework:
- plugin-local `CTest + GoogleTest` is in place
- unit and integration tests are active
- `ToolKitNetworkingCore` and `ToolKitNetworkingSessionCore` static libs exist

## Architecture State
### Public Facade
- [NetworkManager.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkManager.h)
- [NetworkManager.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkManager.cpp)

`NetworkManager` remains the ToolKit/editor-facing serialized component.

### Session Layer
- [NetworkSessionTypes.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionTypes.h)
- [NetworkSessionCore.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionCore.h)
- [NetworkSessionCore.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionCore.cpp)
- [NetworkSessionManager.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionManager.h)
- [NetworkSessionManager.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkSessionManager.cpp)

### Bootstrap Provider Layer
- [SessionBootstrapProvider.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/SessionBootstrapProvider.h)
- [SessionBootstrapProvider.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/SessionBootstrapProvider.cpp)

Current provider behavior:
- `DirectAddress` is implemented
- `SessionDirectory` requires an injected `ISessionDirectoryService`
- normal runtime composition now builds a broker-backed `SessionDirectory` service from broker config
- process-local `SessionDirectory` remains available only by explicit injection for tests/local fake-broker scenarios
- `LanDiscovery` and `BrokeredHostedSession` remain explicit unsupported stubs
- unsupported methods still fail with `DisconnectReason::BootstrapFailed`

### Replication / Handshake
- [ReplicationManager.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/ReplicationManager.h)
- [ReplicationManager.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/ReplicationManager.cpp)
- [NetworkPackets.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkPackets.h)

Current state:
- authenticated handshake is required before gameplay traffic
- direct-connect session flow exists for dedicated and listen-server modes
- bootstrap/session provider layer now resolves join/host metadata before transport start

## Phase 6 Endpoint Work Already Done
- `BindAddress`, `AdvertisedAddress`, and `JoinTarget` semantics are separated
- `NetworkManager` exposes config for:
  - `ConnectHost`
  - `ConnectPort`
  - `ListenPort`
  - `BindAddress`
  - `AdvertisedAddress`
  - `MaxClients`
  - `SessionId`
  - `JoinCredential`
  - `RequireJoinCredential`
  - `BuildCompatibilityId`
- `GameServer` now honors configured bind address
- tests were added for endpoint precedence and split connect/listen overrides

## Phase 7 Bootstrap Work Already Done
- provider seam added before transport start
- session manager now resolves join/host through bootstrap providers
- direct provider is the real v1 path
- unsupported provider types fail explicitly instead of being implied
- tests cover:
  - direct provider resolution
  - unsupported provider failure
  - custom provider rewriting resolved join target and session metadata

## Security Direction
Read first:
- [NETWORK_SECURITY_AND_ENDPOINT_ROADMAP.md](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/NETWORK_SECURITY_AND_ENDPOINT_ROADMAP.md)
- [PRODUCTION_NETWORKING_PLAN.md](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/PRODUCTION_NETWORKING_PLAN.md)

Current security position:
- handshake/auth exists
- endpoint semantics are explicit
- provider seam exists
- internet-facing security is not finished

Current active step:
- harden and validate the new runtime broker-backed `SessionDirectory` path before relay work

## Recommended Next Step
Proceed with the next Phase 8 slice: production-hardening for the runtime broker-backed `SessionDirectory` path.

Priority items:
1. Broker trust/auth policy
- HTTPS-only production mode
- auth token sourcing and validation
- fail-closed behavior for missing/invalid broker config

2. Real broker compatibility
- verify runtime register/lookup/refresh/unregister against a real backend shape
- tighten route metadata and expiry semantics for future relay compatibility

3. Coverage
- runtime broker config validation
- runtime service composition
- timeout/unauthorized/protocol-error broker cases
- keep explicit process-local injection coverage for local-only tests

## Test Status
Current test files:
- [NetworkSessionTypesTests.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Tests/Unit/NetworkSessionTypesTests.cpp)
- [NetworkSessionManagerIntegrationTests.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Tests/Integration/NetworkSessionManagerIntegrationTests.cpp)
- [FakeSessionRuntime.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Tests/Support/FakeSessionRuntime.h)
- [ManualClock.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Tests/Support/ManualClock.h)

Current coverage includes:
- session type defaults
- command-line overrides
- endpoint precedence
- session manager state transitions
- handshake success/reject flows
- timeout behavior
- bootstrap provider resolution and unsupported-provider failure
- handshake helper coverage for packet-size checks, duplicate/stale detection, and rate-limit windows

Missing high-priority security tests:
- broader replay attempts
- more malformed handshake/bootstrap payload combinations
- secret redaction behavior across future broker/relay flows
- staged diagnostics assertions

## Build And Test Commands
### Standalone Plugin Test Targets
Working directory:
- `C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Codes`

Commands used successfully:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_unit_tests
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_integration_tests
```

For replication/auth security changes, enable and build engine-coupled tests too:
```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake -S Plugins/ToolKitNetworking/Codes -B Plugins/ToolKitNetworking/Intermediate/Plugin `
  -G "Visual Studio 17 2022" -A x64 `
  -DTK_NET_BUILD_TESTS=ON -DTK_NET_BUILD_ENGINE_TESTS=ON
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking_engine_tests
```

Run tests:
Working directory:
- `C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Intermediate\Tests`

```powershell
ctest -C Debug --output-on-failure
```

Important:
- from WSL, trigger these Windows commands serially
- do not build `ToolKitNetworking_unit_tests` and `ToolKitNetworking_integration_tests` in parallel because MSVC may fail on a shared `.pdb` lock
- if an attached WSL build dies with `UtilAcceptVsock`, retry it as a detached Windows process that writes to a log and an exit-code file

### Standalone Plugin DLL
Working directory:
- `C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Codes`

```powershell
$env:APPDATA='C:/Users/erendegirmenci/toolkit/eren/.codex_tmp/AppData'
cmake --build Intermediate\Plugin --config Debug --target ToolKitNetworking
```

Recommended from WSL for long plugin DLL builds:
```powershell
Start-Process -FilePath 'C:\Users\erendegirmenci\toolkit\eren\Plugins\ToolKitNetworking\Scripts\run_windows_plugin_build.cmd' `
  -WindowStyle Hidden
```

Use the sibling helpers the same way for:
- detached `ctest`: `Plugins/ToolKitNetworking/Scripts/run_windows_ctest.cmd`
- detached generated-project build: `Plugins/ToolKitNetworking/Scripts/run_windows_project_build.cmd`

Poll results from:
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\plugin_build.log`
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\plugin_build.exit`
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\plugin_ctest.log`
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\plugin_ctest.exit`
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\project_build.log`
- `C:\Users\erendegirmenci\toolkit\eren\.codex_tmp\project_build.exit`

Interop/config note:
- if plugin-tree reconfigure unexpectedly loses the ToolKit engine path, verify the real roaming AppData file at `%APPDATA%\ToolKit\Config\Path.txt`
- for this workspace the expected contents are `C:/Users/erendegirmenci/Desktop/Projects/GDTK`

### Generated Project Build
Working directory:
- `C:\Users\erendegirmenci\toolkit\eren`

```powershell
cmake --build Intermediate\Plugin --config Debug
```

## Latest Verified Outputs
- Standalone plugin DLL:
  [ToolKitNetworkingd.dll](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/Bin/ToolKitNetworkingd.dll)
- Generated project DLL:
  [erend.dll](C:/Users/erendegirmenci/toolkit/eren/Codes/Bin/erend.dll)

## Known Warnings
Still present and not fixed yet:
- `C4275` on [NetworkManager.h](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkManager.h)
- legacy `strncpy` / `strcpy` / `inet_ntoa` warnings in:
  - [NetworkManager.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/NetworkManager.cpp)
  - [ReplicationManager.cpp](C:/Users/erendegirmenci/toolkit/eren/Plugins/ToolKitNetworking/Codes/ReplicationManager.cpp)

These are warnings only, not current blockers.

## Agent Directives For The Next Session
- Keep `NetworkManager` as the public facade.
- Do not reintroduce localhost-only assumptions into public runtime paths.
- Do not infer public endpoints from local interface inspection.
- Treat `SessionId` as public metadata and join credentials as secrets.
- Keep replication topology-agnostic.
- Add tests for every new bootstrap/security failure path.
- Prefer dedicated-server production guarantees first, then hosted-session usability.
