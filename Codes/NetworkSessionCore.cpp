#include "NetworkSessionCore.h"
#include <vector>

namespace ToolKit::ToolKitNetworking {
namespace {
bool TryParseOptionValue(const String &arg, const char *prefix, String &value) {
  const String key = prefix;
  if (arg.rfind(key + "=", 0) == 0) {
    value = arg.substr(key.size() + 1);
    return true;
  }

  return false;
}

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
          overrides.hasConnectPortOverride = true;
          overrides.connectPort = static_cast<uint16_t>(parsedPort);
          overrides.hasListenPortOverride = true;
          overrides.listenPort = static_cast<uint16_t>(parsedPort);
        }
      } catch (...) {
      }
      ++idx;
      continue;
    }

    String optionValue;
    if (TryParseOptionValue(arg, "-connectHost", optionValue)) {
      overrides.hasConnectHostOverride = true;
      overrides.connectHost = optionValue;
      continue;
    }

    if (TryParseOptionValue(arg, "-connectPort", optionValue)) {
      try {
        const int parsedPort = std::stoi(optionValue);
        if (parsedPort > 0 && parsedPort <= 65535) {
          overrides.hasConnectPortOverride = true;
          overrides.connectPort = static_cast<uint16_t>(parsedPort);
        }
      } catch (...) {
      }
      continue;
    }

    if (TryParseOptionValue(arg, "-listenPort", optionValue)) {
      try {
        const int parsedPort = std::stoi(optionValue);
        if (parsedPort > 0 && parsedPort <= 65535) {
          overrides.hasListenPortOverride = true;
          overrides.listenPort = static_cast<uint16_t>(parsedPort);
        }
      } catch (...) {
      }
      continue;
    }

    if (TryParseOptionValue(arg, "-bindAddress", optionValue)) {
      overrides.hasBindAddressOverride = true;
      overrides.bindAddress = optionValue;
      continue;
    }

    if (TryParseOptionValue(arg, "-advertisedAddress", optionValue)) {
      overrides.hasAdvertisedAddressOverride = true;
      overrides.advertisedAddress = optionValue;
      continue;
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
  request.bindEndpoint.host =
      overrides.hasBindAddressOverride ? overrides.bindAddress : config.bindAddress;
  request.bindEndpoint.port =
      overrides.hasListenPortOverride ? overrides.listenPort : config.listenPort;
  request.advertisedEndpoint.usage = EndpointUsage::Advertised;
  request.advertisedEndpoint.protocol = TransportProtocol::EnetUdp;
  request.advertisedEndpoint.host = overrides.hasAdvertisedAddressOverride
                                        ? overrides.advertisedAddress
                                        : config.advertisedAddress;
  request.advertisedEndpoint.port = request.bindEndpoint.port;
  request.sessionId = config.sessionId;
  request.joinCredential = config.joinCredential;
  request.buildCompatibilityId =
      config.buildCompatibilityId.empty() ? BuildCompatibilityId()
                                          : config.buildCompatibilityId;
  request.maxClients = config.maxClients;
  request.requireJoinCredential = config.requireJoinCredential;
  return request;
}

SessionJoinRequest
SessionCore::BuildJoinRequest(const SessionBootstrapConfig &config,
                              const CommandLineSessionOverrides &overrides) {
  SessionJoinRequest request;
  request.joinMethod = config.joinMethod;
  request.targetEndpoint.usage = EndpointUsage::JoinTarget;
  request.targetEndpoint.protocol = TransportProtocol::EnetUdp;
  if (overrides.hasConnectHostOverride) {
    request.targetEndpoint.host = overrides.connectHost;
  } else if (!config.connectHost.empty()) {
    request.targetEndpoint.host = config.connectHost;
  } else if (config.hostingMode == HostingMode::ListenServer &&
             !config.bindAddress.empty() && config.bindAddress != "0.0.0.0") {
    request.targetEndpoint.host = config.bindAddress;
  } else {
    request.targetEndpoint.host = "127.0.0.1";
  }
  request.targetEndpoint.port =
      overrides.hasConnectPortOverride ? overrides.connectPort : config.connectPort;
  if (config.hostingMode == HostingMode::ListenServer &&
      !overrides.hasConnectPortOverride && request.targetEndpoint.port == 0) {
    request.targetEndpoint.port = config.listenPort;
  }
  request.sessionId = config.sessionId;
  request.joinCredential = config.joinCredential;
  request.buildCompatibilityId =
      config.buildCompatibilityId.empty() ? BuildCompatibilityId()
                                          : config.buildCompatibilityId;
  return request;
}
} // namespace ToolKit::ToolKitNetworking
