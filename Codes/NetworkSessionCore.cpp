#include "NetworkSessionCore.h"
#include <vector>

namespace ToolKit::ToolKitNetworking {
namespace {
std::vector<String> TokenizeCommandLine(const String &commandLine) {
  std::vector<String> args;
  String current;
  bool inQuotes = false;

  for (char ch : commandLine) {
    if (ch == ' ' && !inQuotes) {
      if (!current.empty()) {
        args.push_back(current);
        current.clear();
      }
      continue;
    }

    if (ch == '"') {
      inQuotes = !inQuotes;
      continue;
    }

    current += ch;
  }

  if (!current.empty()) {
    args.push_back(current);
  }

  return args;
}
} // namespace

String SessionCore::BuildCompatibilityId() {
  return std::to_string(SessionProtocol::Version) + "." +
         std::to_string(SessionProtocol::BuildCompatibilityRevision);
}

HostingMode SessionCore::LegacyRoleToHostingMode(NetworkRole role) {
  switch (role) {
  case NetworkRole::Client:
    return HostingMode::Client;
  case NetworkRole::DedicatedServer:
    return HostingMode::DedicatedServer;
  case NetworkRole::Host:
    return HostingMode::ListenServer;
  case NetworkRole::None:
  default:
    return HostingMode::None;
  }
}

CommandLineSessionOverrides
SessionCore::ParseCommandLineOverrides(const String &commandLine) {
  CommandLineSessionOverrides overrides;
  const std::vector<String> args = TokenizeCommandLine(commandLine);

  for (size_t idx = 0; idx < args.size(); ++idx) {
    const String &arg = args[idx];

    if (arg == "-server" || arg == "-dedicated") {
      overrides.hasHostingModeOverride = true;
      overrides.hostingMode = HostingMode::DedicatedServer;
      continue;
    }

    if (arg == "-host") {
      overrides.hasHostingModeOverride = true;
      overrides.hostingMode = HostingMode::ListenServer;
      continue;
    }

    if (arg == "-client") {
      overrides.hasHostingModeOverride = true;
      overrides.hostingMode = HostingMode::Client;
      continue;
    }

    if (arg == "-ip" && idx + 1 < args.size()) {
      overrides.hasConnectHostOverride = true;
      overrides.connectHost = args[idx + 1];
      ++idx;
      continue;
    }

    if (arg == "-port" && idx + 1 < args.size()) {
      try {
        const int parsedPort = std::stoi(args[idx + 1]);
        if (parsedPort > 0 && parsedPort <= 65535) {
          overrides.hasPortOverride = true;
          overrides.port = static_cast<uint16_t>(parsedPort);
        }
      } catch (...) {
      }
      ++idx;
    }
  }

  return overrides;
}

SessionHostRequest
SessionCore::BuildHostRequest(const SessionBootstrapConfig &config,
                              const CommandLineSessionOverrides &overrides) {
  SessionHostRequest request;
  request.hostingMode = overrides.hasHostingModeOverride ? overrides.hostingMode
                                                         : config.hostingMode;
  request.bindEndpoint.usage = EndpointUsage::Bind;
  request.bindEndpoint.protocol = TransportProtocol::EnetUdp;
  request.bindEndpoint.host = config.bindAddress;
  request.bindEndpoint.port =
      overrides.hasPortOverride ? overrides.port : config.listenPort;
  request.advertisedEndpoint.usage = EndpointUsage::Advertised;
  request.advertisedEndpoint.protocol = TransportProtocol::EnetUdp;
  request.advertisedEndpoint.host = config.advertisedAddress;
  request.advertisedEndpoint.port = request.bindEndpoint.port;
  request.buildCompatibilityId =
      config.buildCompatibilityId.empty() ? BuildCompatibilityId()
                                          : config.buildCompatibilityId;
  request.maxClients = config.maxClients;
  return request;
}

SessionJoinRequest
SessionCore::BuildJoinRequest(const SessionBootstrapConfig &config,
                              const CommandLineSessionOverrides &overrides) {
  SessionJoinRequest request;
  request.joinMethod = config.joinMethod;
  request.targetEndpoint.usage = EndpointUsage::JoinTarget;
  request.targetEndpoint.protocol = TransportProtocol::EnetUdp;
  request.targetEndpoint.host =
      overrides.hasConnectHostOverride ? overrides.connectHost : config.connectHost;
  request.targetEndpoint.port =
      overrides.hasPortOverride ? overrides.port : config.connectPort;
  request.buildCompatibilityId =
      config.buildCompatibilityId.empty() ? BuildCompatibilityId()
                                          : config.buildCompatibilityId;
  return request;
}
} // namespace ToolKit::ToolKitNetworking
