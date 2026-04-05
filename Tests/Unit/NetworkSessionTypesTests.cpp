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

TEST(NetworkSessionTypesTest, CommandLineOverridesParseLegacyHostAndPortFlags) {
  const CommandLineSessionOverrides overrides =
      SessionCore::ParseCommandLineOverrides(
          "\"ToolKitNetworking.exe\" -host -ip 10.0.0.25 -port 9000");

  EXPECT_TRUE(overrides.hasHostingModeOverride);
  EXPECT_EQ(overrides.hostingMode, HostingMode::ListenServer);
  EXPECT_TRUE(overrides.hasConnectHostOverride);
  EXPECT_EQ(overrides.connectHost, "10.0.0.25");
  EXPECT_TRUE(overrides.hasConnectPortOverride);
  EXPECT_EQ(overrides.connectPort, 9000);
  EXPECT_TRUE(overrides.hasListenPortOverride);
  EXPECT_EQ(overrides.listenPort, 9000);
}

TEST(NetworkSessionTypesTest, CommandLineOverridesParseExplicitEndpointFlags) {
  const CommandLineSessionOverrides overrides =
      SessionCore::ParseCommandLineOverrides(
          "\"ToolKitNetworking.exe\" -client -connectHost=wan.example.net "
          "-connectPort=7001 -listenPort=7002 -bindAddress=0.0.0.0 "
          "-advertisedAddress=203.0.113.10");

  EXPECT_TRUE(overrides.hasHostingModeOverride);
  EXPECT_EQ(overrides.hostingMode, HostingMode::Client);
  EXPECT_TRUE(overrides.hasConnectHostOverride);
  EXPECT_EQ(overrides.connectHost, "wan.example.net");
  EXPECT_TRUE(overrides.hasConnectPortOverride);
  EXPECT_EQ(overrides.connectPort, 7001);
  EXPECT_TRUE(overrides.hasListenPortOverride);
  EXPECT_EQ(overrides.listenPort, 7002);
  EXPECT_TRUE(overrides.hasBindAddressOverride);
  EXPECT_EQ(overrides.bindAddress, "0.0.0.0");
  EXPECT_TRUE(overrides.hasAdvertisedAddressOverride);
  EXPECT_EQ(overrides.advertisedAddress, "203.0.113.10");
}

TEST(NetworkSessionTypesTest, CoreBuildsJoinRequestFromConfigAndOverrides) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::Client;
  config.connectHost = "192.168.1.20";
  config.connectPort = 7777;

  CommandLineSessionOverrides overrides;
  overrides.hasConnectHostOverride = true;
  overrides.connectHost = "10.1.1.5";
  overrides.hasConnectPortOverride = true;
  overrides.connectPort = 8888;

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

TEST(NetworkSessionTypesTest, HostRequestUsesBindAndAdvertisedEndpointOverrides) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::DedicatedServer;
  config.listenPort = 7777;
  config.bindAddress = "0.0.0.0";
  config.advertisedAddress = "198.51.100.10";

  CommandLineSessionOverrides overrides;
  overrides.hasListenPortOverride = true;
  overrides.listenPort = 9002;
  overrides.hasBindAddressOverride = true;
  overrides.bindAddress = "192.168.1.25";
  overrides.hasAdvertisedAddressOverride = true;
  overrides.advertisedAddress = "203.0.113.77";

  const SessionHostRequest request =
      SessionCore::BuildHostRequest(config, overrides);

  EXPECT_EQ(request.bindEndpoint.host, "192.168.1.25");
  EXPECT_EQ(request.bindEndpoint.port, 9002);
  EXPECT_EQ(request.advertisedEndpoint.host, "203.0.113.77");
  EXPECT_EQ(request.advertisedEndpoint.port, 9002);
}

TEST(NetworkSessionTypesTest, ListenServerJoinFallsBackToBindAddressWhenConnectHostIsEmpty) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::ListenServer;
  config.connectHost.clear();
  config.connectPort = 0;
  config.listenPort = 7777;
  config.bindAddress = "192.168.1.20";

  const SessionJoinRequest request =
      SessionCore::BuildJoinRequest(config, CommandLineSessionOverrides{});

  EXPECT_EQ(request.targetEndpoint.host, "192.168.1.20");
  EXPECT_EQ(request.targetEndpoint.port, 7777);
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
