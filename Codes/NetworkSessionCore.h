#pragma once

#include "NetworkRole.h"
#include "NetworkSessionTypes.h"

namespace ToolKit::ToolKitNetworking {
struct CommandLineSessionOverrides {
  bool hasHostingModeOverride = false;
  HostingMode hostingMode = HostingMode::None;

  bool hasConnectHostOverride = false;
  String connectHost = "127.0.0.1";

  bool hasConnectPortOverride = false;
  uint16_t connectPort = 8080;

  bool hasListenPortOverride = false;
  uint16_t listenPort = 8080;

  bool hasBindAddressOverride = false;
  String bindAddress;

  bool hasAdvertisedAddressOverride = false;
  String advertisedAddress;
};

struct SessionBootstrapConfig {
  HostingMode hostingMode = HostingMode::None;
  JoinMethod joinMethod = JoinMethod::DirectAddress;
  String connectHost = "127.0.0.1";
  uint16_t connectPort = 8080;
  uint16_t listenPort = 8080;
  String bindAddress;
  String advertisedAddress;
  String buildCompatibilityId;
  String sessionId;
  String joinCredential;
  uint maxClients = 2;
  bool requireJoinCredential = false;
};

namespace SessionCore {
String BuildCompatibilityId();
HostingMode LegacyRoleToHostingMode(NetworkRole role);
CommandLineSessionOverrides ParseCommandLineOverrides(const String &commandLine);
SessionHostRequest BuildHostRequest(const SessionBootstrapConfig &config,
                                    const CommandLineSessionOverrides &overrides);
SessionJoinRequest BuildJoinRequest(const SessionBootstrapConfig &config,
                                    const CommandLineSessionOverrides &overrides);
} // namespace SessionCore
} // namespace ToolKit::ToolKitNetworking
