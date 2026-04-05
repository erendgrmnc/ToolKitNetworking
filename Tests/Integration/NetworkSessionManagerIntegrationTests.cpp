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
  runtime.configuredBootstrapConfig.joinMethod = JoinMethod::SessionDirectory;

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  EXPECT_FALSE(manager.StartConfiguredSession());
  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::BootstrapFailed);
  EXPECT_EQ(runtime.startClientCalls, 0);
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

  NetworkSessionManager manager(runtime, []() {
    return CommandLineSessionOverrides{};
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  runtime.clientConnected = true;
  manager.Update();
  runtime.sessionAuthFailed = true;
  runtime.authFailureReason = DisconnectReason::AuthRejected;
  runtime.authFailureDetail = "Join credential rejected.";
  manager.Update();

  EXPECT_EQ(manager.GetConnectionStatus().state, ConnectionState::Failed);
  EXPECT_EQ(manager.GetConnectionStatus().disconnectReason,
            DisconnectReason::AuthRejected);
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
