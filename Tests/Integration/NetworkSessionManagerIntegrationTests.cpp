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
  });

  ASSERT_TRUE(hostManager.StartConfiguredSession());
  ASSERT_FALSE(hostManager.GetLastHostRequest().sessionId.empty());
  ASSERT_FALSE(hostManager.GetLastHostRequest().joinCredential.empty());
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
