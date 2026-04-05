#include "NetworkSessionManager.h"
#include "SessionBootstrapProvider.h"
#include "Support/FakeSessionRuntime.h"
#include "Support/ManualClock.h"
#include <gtest/gtest.h>

namespace ToolKit::ToolKitNetworking {
namespace {
class RewritingBootstrapProvider : public ISessionBootstrapProvider {
public:
  JoinMethod GetJoinMethod() const override {
    return JoinMethod::BrokeredHostedSession;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.success = true;
    result.request = request;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = request.advertisedEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.success = true;
    result.request = request;
    result.request.targetEndpoint.host = "relay.example.net";
    result.request.targetEndpoint.port = 9100;
    result.request.sessionId = "resolved-session-id";
    result.request.joinCredential = "resolved-secret";
    result.session.sessionId = "resolved-session-id";
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.resolvedEndpoint = result.request.targetEndpoint;
    result.session.buildCompatibilityId = "resolved-build";
    result.session.relayRequired = true;
    result.session.isJoinCredentialRequired = true;
    return result;
  }
};

class IncompleteJoinBootstrapProvider : public ISessionBootstrapProvider {
public:
  JoinMethod GetJoinMethod() const override {
    return JoinMethod::BrokeredHostedSession;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.success = true;
    result.request = request;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = request.advertisedEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.success = true;
    result.request = request;
    result.request.targetEndpoint = NetworkEndpoint{};
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    return result;
  }
};

class ResolvedEndpointOnlyBootstrapProvider : public ISessionBootstrapProvider {
public:
  JoinMethod GetJoinMethod() const override {
    return JoinMethod::BrokeredHostedSession;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.success = true;
    result.request = request;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = request.advertisedEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.success = true;
    result.request = request;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = JoinMethod::BrokeredHostedSession;
    result.session.resolvedEndpoint.host = "resolved-only.example.net";
    result.session.resolvedEndpoint.port = 9200;
    result.session.resolvedEndpoint.usage = EndpointUsage::ResolvedTransport;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    return result;
  }
};

struct SessionDirectoryLifecycleState {
  String registrationHandle = "managed-registration";
  uint64_t registrationExpiresAtMs = 60000;
  int refreshCalls = 0;
  int releaseCalls = 0;
  int remainingReleaseFailures = 0;
  bool failRefresh = false;
  DisconnectReason refreshFailureReason = DisconnectReason::TransportError;
  String refreshFailureDetail = "Refresh rejected.";
  DisconnectReason releaseFailureReason = DisconnectReason::TransportError;
  String releaseFailureDetail = "Release rejected.";
};

class ManagedSessionDirectoryBootstrapProvider : public ISessionBootstrapProvider {
public:
  explicit ManagedSessionDirectoryBootstrapProvider(
      std::shared_ptr<SessionDirectoryLifecycleState> state)
      : m_state(std::move(state)) {}

  JoinMethod GetJoinMethod() const override {
    return JoinMethod::SessionDirectory;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.success = true;
    result.request = request;
    result.request.sessionId = request.sessionId.empty() ? "managed-session"
                                                         : request.sessionId;
    result.request.joinCredential =
        request.joinCredential.empty() ? "managed-secret" : request.joinCredential;
    result.request.directoryRegistrationHandle = m_state->registrationHandle;
    result.request.directoryRegistrationExpiresAtMs =
        m_state->registrationExpiresAtMs;
    result.request.requireJoinCredential = true;
    result.session.sessionId = result.request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::SessionDirectory;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = request.advertisedEndpoint;
    result.session.resolvedEndpoint = request.advertisedEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = true;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.success = true;
    result.request = request;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = JoinMethod::SessionDirectory;
    result.session.resolvedEndpoint = request.targetEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = !request.joinCredential.empty();
    return result;
  }

  HostedSessionRefreshResult RefreshHostedSession(
      const SessionHostRequest &) override {
    ++m_state->refreshCalls;
    HostedSessionRefreshResult result;
    if (m_state->failRefresh) {
      result.success = false;
      result.disconnectReason = m_state->refreshFailureReason;
      result.detailMessage = m_state->refreshFailureDetail;
      return result;
    }

    m_state->registrationExpiresAtMs += 60000;
    result.registrationExpiresAtMs = m_state->registrationExpiresAtMs;
    return result;
  }

  HostedSessionReleaseResult ReleaseHostedSession(
      const SessionHostRequest &) override {
    ++m_state->releaseCalls;
    if (m_state->remainingReleaseFailures > 0) {
      --m_state->remainingReleaseFailures;
      HostedSessionReleaseResult result;
      result.success = false;
      result.disconnectReason = m_state->releaseFailureReason;
      result.detailMessage = m_state->releaseFailureDetail;
      return result;
    }
    return HostedSessionReleaseResult{};
  }

private:
  std::shared_ptr<SessionDirectoryLifecycleState> m_state;
};
} // namespace

TEST(NetworkSessionManagerIntegrationTest, DedicatedServerStartsServerOnly) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::DedicatedServer;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.startServerCalls, 1);
  EXPECT_EQ(runtime.startClientCalls, 0);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connected);
  EXPECT_TRUE(manager.GetConnectionStatus().isAuthenticated);
}

TEST(NetworkSessionManagerIntegrationTest, ClientTransitionsToConnectedAfterTransportConnect) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  }, [&clock]() { return clock.NowMs(); });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connecting);
  runtime.clientConnected = true;
  manager.Update();
  EXPECT_EQ(runtime.beginHandshakeCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Handshaking);
  runtime.sessionAuthenticated = true;
  manager.Update();
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connected);
  EXPECT_TRUE(manager.GetConnectionStatus().isAuthenticated);
}

TEST(NetworkSessionManagerIntegrationTest, ListenServerStartsServerAndClient) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  runtime.configuredHostingMode = HostingMode::ListenServer;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  }, [&clock]() { return clock.NowMs(); });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.startServerCalls, 1);
  EXPECT_EQ(runtime.startClientCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connecting);
  runtime.clientConnected = true;
  manager.Update();
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Handshaking);
  runtime.sessionAuthenticated = true;
  manager.Update();
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connected);
  EXPECT_TRUE(manager.GetConnectionStatus().isAuthenticated);
}

TEST(NetworkSessionManagerIntegrationTest, OverridesAreAppliedToTransportRequests) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    CommandLineSessionOverrides overrides;
    overrides.hasConnectHostOverride = true;
    overrides.connectHost = "10.10.1.12";
    overrides.hasConnectPortOverride = true;
    overrides.connectPort = 9001;
    return overrides;
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.lastClientHost, "10.10.1.12");
  EXPECT_EQ(runtime.lastClientPort, 9001);
}

TEST(NetworkSessionManagerIntegrationTest, ConfiguredBootstrapEndpointsAreUsedWithoutOverrides) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.connectHost = "dedicated.example.net";
  runtime.configuredBootstrapConfig.connectPort = 7007;
  runtime.configuredBootstrapConfig.sessionId = "session-alpha";
  runtime.configuredBootstrapConfig.buildCompatibilityId = "build-42";

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.lastClientHost, "dedicated.example.net");
  EXPECT_EQ(runtime.lastClientPort, 7007);
  EXPECT_EQ(manager.GetLastJoinRequest().sessionId, "session-alpha");
  EXPECT_EQ(manager.GetLastJoinRequest().buildCompatibilityId, "build-42");
}

TEST(NetworkSessionManagerIntegrationTest, UnsupportedBootstrapMethodFailsBeforeTransportStart) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::LanDiscovery;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::BootstrapFailed);
  EXPECT_EQ(runtime.startClientCalls, 0);
}

TEST(NetworkSessionManagerIntegrationTest, SessionDirectoryHostRegistersAndClientResolvesSession) {
  SessionDirectoryServicePtr sharedDirectory =
      CreateProcessLocalSessionDirectoryService();
  FakeSessionRuntime hostRuntime;
  hostRuntime.configuredHostingMode = HostingMode::DedicatedServer;
  hostRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  hostRuntime.configuredBootstrapConfig.listenPort = 7777;
  hostRuntime.configuredBootstrapConfig.bindAddress = "192.168.1.20";
  hostRuntime.configuredBootstrapConfig.advertisedAddress =
      "directory.example.net";
  hostRuntime.configuredBootstrapConfig.buildCompatibilityId = "build-dir-2";

  NetworkSessionManager hostManager(hostRuntime, []() {
    return CommandLineSessionOverrides{};
  }, {}, [sharedDirectory](JoinMethod joinMethod) mutable {
    return CreateBootstrapProvider(joinMethod, sharedDirectory);
  });

  ASSERT_TRUE(hostManager.StartConfiguredSession());
  ASSERT_FALSE(hostManager.GetLastHostRequest().sessionId.empty());
  ASSERT_FALSE(hostManager.GetLastHostRequest().joinCredential.empty());
  ASSERT_FALSE(hostManager.GetLastHostRequest().directoryRegistrationHandle.empty());
  EXPECT_TRUE(hostManager.GetLastHostRequest().requireJoinCredential);
  EXPECT_EQ(hostManager.GetActiveSession().advertisedEndpoint.host,
            "directory.example.net");

  FakeSessionRuntime clientRuntime;
  clientRuntime.configuredHostingMode = HostingMode::Client;
  clientRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  clientRuntime.configuredBootstrapConfig.sessionId =
      hostManager.GetLastHostRequest().sessionId;
  clientRuntime.configuredBootstrapConfig.buildCompatibilityId = "build-dir-2";

  NetworkSessionManager clientManager(clientRuntime, []() {
    return CommandLineSessionOverrides{};
  }, {}, [sharedDirectory](JoinMethod joinMethod) mutable {
    return CreateBootstrapProvider(joinMethod, sharedDirectory);
  });

  ASSERT_TRUE(clientManager.StartConfiguredSession());
  EXPECT_EQ(clientRuntime.lastClientHost, "directory.example.net");
  EXPECT_EQ(clientRuntime.lastClientPort, 7777);
  EXPECT_EQ(clientManager.GetLastJoinRequest().sessionId,
            hostManager.GetLastHostRequest().sessionId);
  EXPECT_EQ(clientManager.GetLastJoinRequest().joinCredential,
            hostManager.GetLastHostRequest().joinCredential);
  EXPECT_EQ(clientManager.GetActiveSession().joinMethod,
            JoinMethod::SessionDirectory);
  EXPECT_EQ(clientManager.GetActiveSession().hostingMode,
            HostingMode::DedicatedServer);
}

TEST(NetworkSessionManagerIntegrationTest, SessionDirectoryUnknownSessionFailsBeforeTransportStart) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.sessionId = "missing-session";

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::SessionClosed);
  EXPECT_EQ(runtime.startClientCalls, 0);
}

TEST(NetworkSessionManagerIntegrationTest, SessionDirectoryBuildMismatchFailsBeforeTransportStart) {
  SessionDirectoryServicePtr sharedDirectory =
      CreateProcessLocalSessionDirectoryService();
  FakeSessionRuntime hostRuntime;
  hostRuntime.configuredHostingMode = HostingMode::DedicatedServer;
  hostRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  hostRuntime.configuredBootstrapConfig.listenPort = 7778;
  hostRuntime.configuredBootstrapConfig.bindAddress = "192.168.1.21";
  hostRuntime.configuredBootstrapConfig.advertisedAddress =
      "directory.example.net";
  hostRuntime.configuredBootstrapConfig.buildCompatibilityId = "build-dir-host";

  NetworkSessionManager hostManager(hostRuntime, []() {
    return CommandLineSessionOverrides{};
  }, {}, [sharedDirectory](JoinMethod joinMethod) mutable {
    return CreateBootstrapProvider(joinMethod, sharedDirectory);
  });

  ASSERT_TRUE(hostManager.StartConfiguredSession());

  FakeSessionRuntime clientRuntime;
  clientRuntime.configuredHostingMode = HostingMode::Client;
  clientRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  clientRuntime.configuredBootstrapConfig.sessionId =
      hostManager.GetLastHostRequest().sessionId;
  clientRuntime.configuredBootstrapConfig.buildCompatibilityId =
      "build-dir-client";

  NetworkSessionManager clientManager(clientRuntime, []() {
    return CommandLineSessionOverrides{};
  }, {}, [sharedDirectory](JoinMethod joinMethod) mutable {
    return CreateBootstrapProvider(joinMethod, sharedDirectory);
  });

  EXPECT_FALSE(clientManager.StartConfiguredSession());
  EXPECT_EQ(clientManager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(clientManager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::VersionMismatch);
  EXPECT_EQ(clientRuntime.startClientCalls, 0);
}

TEST(NetworkSessionManagerIntegrationTest, SessionDirectoryHostedRegistrationRefreshesBeforeExpiry) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  state->registrationExpiresAtMs = 60000;
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7780;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; },
      [&clock]() { return clock.NowMs(); },
      [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::SessionDirectory) {
          return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetLastHostRequest().directoryRegistrationExpiresAtMs, 60000u);

  clock.SetNowMs(29999);
  manager.Update();
  EXPECT_EQ(state->refreshCalls, 0);

  clock.SetNowMs(30000);
  manager.Update();
  EXPECT_EQ(state->refreshCalls, 1);
  EXPECT_EQ(manager.GetLastHostRequest().directoryRegistrationExpiresAtMs,
            120000u);
}

TEST(NetworkSessionManagerIntegrationTest, SessionDirectoryHostedRefreshFailureStopsAndReleasesSession) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  state->registrationExpiresAtMs = 30000;
  state->failRefresh = true;
  state->refreshFailureReason = DisconnectReason::Timeout;
  state->refreshFailureDetail = "Directory lease refresh timed out.";
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7781;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; },
      [&clock]() { return clock.NowMs(); },
      [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::SessionDirectory) {
          return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());

  clock.SetNowMs(15000);
  manager.Update();

  EXPECT_EQ(state->refreshCalls, 1);
  EXPECT_EQ(state->releaseCalls, 1);
  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::Timeout);
}

TEST(NetworkSessionManagerIntegrationTest, StopSessionUnregistersHostedSessionDirectoryRegistration) {
  FakeSessionRuntime runtime;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7782;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; }, {},
      [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::SessionDirectory) {
          return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());

  manager.StopSession();

  EXPECT_EQ(state->releaseCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Disconnected);
  EXPECT_TRUE(manager.GetLastHostRequest().directoryRegistrationHandle.empty());
}

TEST(NetworkSessionManagerIntegrationTest,
     StopSessionRetryableReleaseFailureBlocksNextStartUntilReleaseSucceeds) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  state->remainingReleaseFailures = 3;
  state->releaseFailureReason = DisconnectReason::TransportError;
  state->releaseFailureDetail = "Directory unregister transport failed.";
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7783;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; },
      [&clock]() { return clock.NowMs(); },
      [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::SessionDirectory) {
          return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());

  manager.StopSession();
  EXPECT_EQ(state->releaseCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Disconnected);
  EXPECT_NE(manager.GetConnectionStatus().detailMessage.find("Directory unregister transport failed."),
            String::npos);

  clock.SetNowMs(4999);
  manager.Update();
  EXPECT_EQ(state->releaseCalls, 1);

  clock.SetNowMs(5000);
  manager.Update();
  EXPECT_EQ(state->releaseCalls, 2);

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(state->releaseCalls, 3);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::BootstrapFailed);
  EXPECT_NE(manager.GetConnectionStatus().bootstrapDetail.find("Directory unregister transport failed."),
            String::npos);

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(state->releaseCalls, 4);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connected);
}

TEST(NetworkSessionManagerIntegrationTest,
     StartupReplacementAlsoAbortsOnTerminalReleaseFailure) {
  FakeSessionRuntime runtime;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  state->remainingReleaseFailures = 1;
  state->releaseFailureReason = DisconnectReason::ProtocolError;
  state->releaseFailureDetail = "Directory unregister response was malformed.";
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7787;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; }, {},
      [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::SessionDirectory) {
          return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());
  manager.StopSession();
  EXPECT_EQ(state->releaseCalls, 1);

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(state->releaseCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::BootstrapFailed);
  EXPECT_NE(manager.GetConnectionStatus().bootstrapDetail.find("malformed"),
            String::npos);
  EXPECT_EQ(state->releaseCalls, 1);

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(state->releaseCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::BootstrapFailed);
}

TEST(NetworkSessionManagerIntegrationTest,
     NetworkSessionManagerDestructorReleasesHostedSessionRegistration) {
  FakeSessionRuntime runtime;
  auto state = std::make_shared<SessionDirectoryLifecycleState>();
  runtime.configuredHostingMode = HostingMode::DedicatedServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  runtime.configuredBootstrapConfig.listenPort = 7784;
  runtime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";

  {
    NetworkSessionManager manager(
        runtime, []() { return CommandLineSessionOverrides{}; }, {},
        [state](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
          if (joinMethod == JoinMethod::SessionDirectory) {
            return std::make_unique<ManagedSessionDirectoryBootstrapProvider>(state);
          }
          return CreateBootstrapProvider(joinMethod);
        });

    ASSERT_TRUE(manager.StartConfiguredSession());
  }

  EXPECT_EQ(state->releaseCalls, 1);
}

TEST(NetworkSessionManagerIntegrationTest,
     DefaultSessionDirectoryProviderSupportsZeroConfigLocalInterop) {
  FakeSessionRuntime hostRuntime;
  hostRuntime.configuredHostingMode = HostingMode::DedicatedServer;
  hostRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  hostRuntime.configuredBootstrapConfig.listenPort = 7785;
  hostRuntime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";
  hostRuntime.configuredBootstrapConfig.sessionId = "default-shared-session";

  NetworkSessionManager hostManager(hostRuntime, []() {
    return CommandLineSessionOverrides{};
  });
  ASSERT_TRUE(hostManager.StartConfiguredSession());

  FakeSessionRuntime clientRuntime;
  clientRuntime.configuredHostingMode = HostingMode::Client;
  clientRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  clientRuntime.configuredBootstrapConfig.sessionId =
      hostManager.GetActiveSession().sessionId;

  NetworkSessionManager clientManager(clientRuntime, []() {
    return CommandLineSessionOverrides{};
  });
  ASSERT_TRUE(clientManager.StartConfiguredSession());

  EXPECT_EQ(clientRuntime.startClientCalls, 1);
  EXPECT_EQ(clientManager.GetActiveSession().sessionId,
            hostManager.GetActiveSession().sessionId);

  hostManager.StopSession();
}

TEST(NetworkSessionManagerIntegrationTest,
     SessionDirectoryExpiredCredentialSurfacesAuthRejectedBootstrapFailure) {
  FakeSessionRuntime hostRuntime;
  FakeSessionRuntime clientRuntime;
  ManualClock clock;
  clock.SetNowMs(1000);
  SessionDirectoryServicePtr sharedDirectory =
      CreateProcessLocalSessionDirectoryService([&clock]() { return clock.NowMs(); });

  hostRuntime.configuredHostingMode = HostingMode::DedicatedServer;
  hostRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  hostRuntime.configuredBootstrapConfig.listenPort = 7786;
  hostRuntime.configuredBootstrapConfig.advertisedAddress = "directory.example.net";
  hostRuntime.configuredBootstrapConfig.sessionId = "expiring-session";

  NetworkSessionManager hostManager(
      hostRuntime, []() { return CommandLineSessionOverrides{}; }, {},
      [sharedDirectory](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        return CreateBootstrapProvider(joinMethod, sharedDirectory);
      });

  ASSERT_TRUE(hostManager.StartConfiguredSession());
  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = hostManager.GetActiveSession().sessionId;
  const SessionDirectoryLookupResult lookupResult =
      sharedDirectory->LookupSession(lookup);
  ASSERT_TRUE(lookupResult.success);
  clock.SetNowMs(lookupResult.joinCredentialExpiresAtMs);

  clientRuntime.configuredHostingMode = HostingMode::Client;
  clientRuntime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;
  clientRuntime.configuredBootstrapConfig.sessionId =
      hostManager.GetActiveSession().sessionId;

  NetworkSessionManager clientManager(
      clientRuntime, []() { return CommandLineSessionOverrides{}; }, {},
      [sharedDirectory](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        return CreateBootstrapProvider(joinMethod, sharedDirectory);
      });

  EXPECT_FALSE(clientManager.StartConfiguredSession());
  EXPECT_EQ(clientManager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(clientManager.GetConnectionStatus().disconnectReason,
            DisconnectReason::AuthRejected);
  EXPECT_EQ(clientManager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::AuthRejected);
  EXPECT_EQ(clientManager.GetConnectionStatus().handshakeFailureReason,
            DisconnectReason::None);
  EXPECT_NE(clientManager.GetConnectionStatus().bootstrapDetail.find("expired"),
            String::npos);
}

TEST(NetworkSessionManagerIntegrationTest, BootstrapFailurePreservesEndpointDiagnostics) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::ListenServer;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::BrokeredHostedSession;
  runtime.configuredBootstrapConfig.connectHost = "ignored.example.net";
  runtime.configuredBootstrapConfig.connectPort = 7777;
  runtime.configuredBootstrapConfig.listenPort = 7777;
  runtime.configuredBootstrapConfig.bindAddress = "0.0.0.0";
  runtime.configuredBootstrapConfig.advertisedAddress = "public.example.net";
  runtime.configuredBootstrapConfig.joinCredential = "requested-secret";
  runtime.configuredBootstrapConfig.requireJoinCredential = true;

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; }, {},
      [](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::BrokeredHostedSession) {
          return std::make_unique<IncompleteJoinBootstrapProvider>();
        }
        return CreateBootstrapProvider(joinMethod);
      });

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::BootstrapFailed);
  EXPECT_EQ(manager.GetConnectionStatus().handshakeFailureReason,
            DisconnectReason::None);
  EXPECT_EQ(manager.GetConnectionStatus().bindEndpoint.host, "0.0.0.0");
  EXPECT_EQ(manager.GetConnectionStatus().bindEndpoint.port, 7777);
  EXPECT_EQ(manager.GetConnectionStatus().advertisedEndpoint.host,
            "public.example.net");
  EXPECT_FALSE(manager.GetConnectionStatus().resolvedJoinTarget.IsConfigured());
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapDetail.find("requested-secret"),
            String::npos);
}

TEST(NetworkSessionManagerIntegrationTest, CustomBootstrapProviderCanRewriteJoinTarget) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::BrokeredHostedSession;
  runtime.configuredBootstrapConfig.connectHost = "ignored.example.net";
  runtime.configuredBootstrapConfig.connectPort = 7007;
  runtime.configuredBootstrapConfig.sessionId = "requested-session-id";
  runtime.configuredBootstrapConfig.joinCredential = "requested-secret";

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; }, {},
      [](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::BrokeredHostedSession) {
          return std::make_unique<RewritingBootstrapProvider>();
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.lastClientHost, "relay.example.net");
  EXPECT_EQ(runtime.lastClientPort, 9100);
  EXPECT_EQ(manager.GetLastJoinRequest().sessionId, "resolved-session-id");
  EXPECT_EQ(manager.GetLastJoinRequest().joinCredential, "resolved-secret");
  EXPECT_EQ(manager.GetActiveSession().resolvedEndpoint.host, "relay.example.net");
  EXPECT_TRUE(manager.GetActiveSession().relayRequired);
}

TEST(NetworkSessionManagerIntegrationTest, ClientTransportUsesResolvedSessionEndpointWhenProviderOnlyRewritesDescriptor) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::BrokeredHostedSession;
  runtime.configuredBootstrapConfig.connectHost = "original.example.net";
  runtime.configuredBootstrapConfig.connectPort = 7007;

  NetworkSessionManager manager(
      runtime, []() { return CommandLineSessionOverrides{}; }, {},
      [](JoinMethod joinMethod) -> SessionBootstrapProviderPtr {
        if (joinMethod == JoinMethod::BrokeredHostedSession) {
          return std::make_unique<ResolvedEndpointOnlyBootstrapProvider>();
        }
        return CreateBootstrapProvider(joinMethod);
      });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.lastClientHost, "resolved-only.example.net");
  EXPECT_EQ(runtime.lastClientPort, 9200);
  EXPECT_EQ(manager.GetActiveSession().resolvedEndpoint.host,
            "resolved-only.example.net");
}

TEST(NetworkSessionManagerIntegrationTest, ConnectedTransportTransitionsToHandshaking) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  runtime.clientConnected = true;
  manager.Update();

  EXPECT_EQ(runtime.beginHandshakeCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Handshaking);
}

TEST(NetworkSessionManagerIntegrationTest, HandshakeRejectTransitionsToFailed) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.configuredBootstrapConfig.joinCredential = "requested-secret";

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  runtime.clientConnected = true;
  manager.Update();
  runtime.sessionAuthFailed = true;
  runtime.authFailureReason = DisconnectReason::AuthRejected;
  runtime.authFailureDetail = "Join credential rejected: requested-secret";
  manager.Update();

  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::AuthRejected);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::None);
  EXPECT_EQ(manager.GetConnectionStatus().handshakeFailureReason,
            DisconnectReason::AuthRejected);
  EXPECT_EQ(manager.GetConnectionStatus().handshakeDetail.find("requested-secret"),
            String::npos);
}

TEST(NetworkSessionManagerIntegrationTest, RateLimitedHandshakeFailureIsSurfacedSeparately) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  runtime.clientConnected = true;
  manager.Update();
  runtime.sessionAuthFailed = true;
  runtime.authFailureReason = DisconnectReason::RateLimited;
  runtime.authFailureDetail = "Handshake peer is temporarily rate limited.";
  manager.Update();

  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::RateLimited);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::None);
  EXPECT_EQ(manager.GetConnectionStatus().handshakeFailureReason,
            DisconnectReason::RateLimited);
}

TEST(NetworkSessionManagerIntegrationTest, ClientConnectTimeoutTransitionsToFailed) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    CommandLineSessionOverrides overrides;
    overrides.hasConnectPortOverride = true;
    overrides.connectPort = 9001;
    return overrides;
  }, [&clock]() { return clock.NowMs(); });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connecting);

  clock.AdvanceMs(SessionProtocol::DefaultConnectionTimeoutMs - 1);
  manager.Update();
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Connecting);

  clock.AdvanceMs(1);
  manager.Update();
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::Timeout);
  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_FALSE(runtime.hasClientTransport);
}

TEST(NetworkSessionManagerIntegrationTest, ListenServerTimeoutStopsBothTransports) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  runtime.configuredHostingMode = HostingMode::ListenServer;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  }, [&clock]() { return clock.NowMs(); });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_TRUE(runtime.hasServerTransport);
  EXPECT_TRUE(runtime.hasClientTransport);

  clock.AdvanceMs(SessionProtocol::DefaultConnectionTimeoutMs);
  manager.Update();

  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::Timeout);
  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_FALSE(runtime.hasServerTransport);
  EXPECT_FALSE(runtime.hasClientTransport);
}

TEST(NetworkSessionManagerIntegrationTest, HandshakeTimeoutPopulatesHandshakeFailureFields) {
  FakeSessionRuntime runtime;
  ManualClock clock;
  clock.SetNowMs(1);
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  }, [&clock]() { return clock.NowMs(); });

  ASSERT_TRUE(manager.StartConfiguredSession());
  runtime.clientConnected = true;
  manager.Update();
  clock.AdvanceMs(SessionProtocol::DefaultHandshakeTimeoutMs);
  manager.Update();

  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().handshakeFailureReason,
            DisconnectReason::Timeout);
  EXPECT_EQ(manager.GetConnectionStatus().bootstrapFailureReason,
            DisconnectReason::None);
}

TEST(NetworkSessionManagerIntegrationTest, StopSessionIsIdempotentAndStopsTransportOnce) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  ASSERT_TRUE(runtime.hasClientTransport);

  manager.StopSession();
  manager.StopSession();

  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Disconnected);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::UserRequested);
}

TEST(NetworkSessionManagerIntegrationTest, FailedClientStartupPropagatesTransportFailure) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.startClientResult = false;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.startClientCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::TransportError);
}

TEST(NetworkSessionManagerIntegrationTest, RestartStopsExistingTransportsBeforeStartingNewSession) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::Client;
  runtime.hasServerTransport = true;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_FALSE(runtime.hasServerTransport);
  EXPECT_EQ(runtime.startClientCalls, 1);
  EXPECT_TRUE(runtime.hasClientTransport);
}

TEST(NetworkSessionManagerIntegrationTest, NoneModeStopsExistingTransportsAndReturnsIdle) {
  FakeSessionRuntime runtime;
  runtime.configuredHostingMode = HostingMode::None;
  runtime.hasServerTransport = true;
  runtime.hasClientTransport = true;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.stopCalls, 1);
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Idle);
  EXPECT_FALSE(runtime.hasServerTransport);
  EXPECT_FALSE(runtime.hasClientTransport);
}
} // namespace ToolKit::ToolKitNetworking
