#include "NetworkSessionManager.h"
#include "Support/FakeSessionRuntime.h"
#include "Support/ManualClock.h"
#include <gtest/gtest.h>

namespace ToolKit::ToolKitNetworking {
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
    overrides.hasPortOverride = true;
    overrides.port = 9001;
    return overrides;
  });

  ASSERT_TRUE(manager.StartConfiguredSession());
  EXPECT_EQ(runtime.lastClientHost, "10.10.1.12");
  EXPECT_EQ(runtime.lastClientPort, 9001);
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
    overrides.hasPortOverride = true;
    overrides.port = 9001;
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
