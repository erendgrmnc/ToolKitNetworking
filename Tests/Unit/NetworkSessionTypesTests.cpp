#include "NetworkRole.h"
#include "NetworkPackets.h"
#include "NetworkSessionCore.h"
#include "NetworkSessionTypes.h"
#include <gtest/gtest.h>

namespace ToolKit::ToolKitNetworking {
TEST(NetworkSessionTypesTest, NetworkEndpointRequiresHostAndPort) {
  NetworkEndpoint endpoint;
  EXPECT_FALSE(endpoint.IsConfigured());

  endpoint.host = "127.0.0.1";
  EXPECT_FALSE(endpoint.IsConfigured());

  endpoint.port = 7777;
  EXPECT_TRUE(endpoint.IsConfigured());
}

TEST(NetworkSessionTypesTest, SessionJoinRequestDefaultsMatchProtocolConstants) {
  SessionJoinRequest request;

  EXPECT_EQ(request.joinMethod, JoinMethod::DirectAddress);
  EXPECT_EQ(request.connectionTimeoutMs,
            SessionProtocol::DefaultConnectionTimeoutMs);
  EXPECT_EQ(request.handshakeTimeoutMs,
            SessionProtocol::DefaultHandshakeTimeoutMs);
  EXPECT_FALSE(request.targetEndpoint.IsConfigured());
}

TEST(NetworkSessionTypesTest, ConnectionStatusStartsUnauthenticated) {
  ConnectionStatus status;

  EXPECT_EQ(status.state, ConnectionState::Idle);
  EXPECT_EQ(status.disconnectReason, DisconnectReason::None);
  EXPECT_FALSE(status.isAuthenticated);
  EXPECT_FALSE(status.activeEndpoint.IsConfigured());
}

TEST(NetworkSessionTypesTest, LegacyRoleMapsToExpectedHostingMode) {
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::None),
            HostingMode::None);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::Client),
            HostingMode::Client);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::DedicatedServer),
            HostingMode::DedicatedServer);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::Host),
            HostingMode::ListenServer);
}

TEST(NetworkSessionTypesTest, CommandLineOverridesParseHostAndPort) {
  const CommandLineSessionOverrides overrides =
      SessionCore::ParseCommandLineOverrides(
          "\"ToolKitNetworking.exe\" -host -ip 10.0.0.25 -port 9000");

  EXPECT_TRUE(overrides.hasHostingModeOverride);
  EXPECT_EQ(overrides.hostingMode, HostingMode::ListenServer);
  EXPECT_TRUE(overrides.hasConnectHostOverride);
  EXPECT_EQ(overrides.connectHost, "10.0.0.25");
  EXPECT_TRUE(overrides.hasPortOverride);
  EXPECT_EQ(overrides.port, 9000);
}

TEST(NetworkSessionTypesTest, CoreBuildsJoinRequestFromConfigAndOverrides) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::Client;
  config.connectHost = "192.168.1.20";
  config.connectPort = 7777;

  CommandLineSessionOverrides overrides;
  overrides.hasConnectHostOverride = true;
  overrides.connectHost = "10.1.1.5";
  overrides.hasPortOverride = true;
  overrides.port = 8888;

  const SessionJoinRequest request =
      SessionCore::BuildJoinRequest(config, overrides);

  EXPECT_EQ(request.targetEndpoint.host, "10.1.1.5");
  EXPECT_EQ(request.targetEndpoint.port, 8888);
  EXPECT_EQ(request.buildCompatibilityId, SessionCore::BuildCompatibilityId());
}

TEST(NetworkSessionTypesTest, HostRequestUsesHostingModeOverride) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::DedicatedServer;
  config.listenPort = 7777;

  CommandLineSessionOverrides overrides;
  overrides.hasHostingModeOverride = true;
  overrides.hostingMode = HostingMode::ListenServer;

  const SessionHostRequest request =
      SessionCore::BuildHostRequest(config, overrides);

  EXPECT_EQ(request.hostingMode, HostingMode::ListenServer);
  EXPECT_EQ(request.bindEndpoint.port, 7777);
}

TEST(NetworkSessionTypesTest, PacketStreamRejectsInvalidSkips) {
  PacketStream stream;
  stream.WriteInt(42);

  EXPECT_FALSE(stream.SkipChecked(-1));
  EXPECT_FALSE(stream.SkipChecked(8));
  EXPECT_TRUE(stream.SkipChecked(4));
  EXPECT_EQ(stream.readOffset, 4);
}
} // namespace ToolKit::ToolKitNetworking
