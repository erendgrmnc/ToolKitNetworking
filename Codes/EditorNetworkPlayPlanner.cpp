/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EditorNetworkPlayPlanner.h"

#include <algorithm>
#include <chrono>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#endif

namespace ToolKit::Editor {
namespace {

String EscapeXml(const String &value) {
  String escaped = value;
  auto replaceAll = [&escaped](const String &needle, const String &replacement) {
    size_t pos = 0;
    while ((pos = escaped.find(needle, pos)) != String::npos) {
      escaped.replace(pos, needle.length(), replacement);
      pos += replacement.length();
    }
  };

  replaceAll("&", "&amp;");
  replaceAll("\"", "&quot;");
  replaceAll("'", "&apos;");
  replaceAll("<", "&lt;");
  replaceAll(">", "&gt;");
  return escaped;
}

String BoolToString(bool value) { return value ? "1" : "0"; }

String TopologyToString(NetworkPlayTopology topology) {
  switch (topology) {
  case NetworkPlayTopology::DedicatedServer:
    return "DedicatedServer";
  case NetworkPlayTopology::ClientAttach:
    return "ClientAttach";
  case NetworkPlayTopology::ListenServer:
  default:
    return "ListenServer";
  }
}

bool IsPortBindable(uint16_t port) {
#ifdef _WIN32
  SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socketHandle == INVALID_SOCKET) {
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  const bool canBind =
      bind(socketHandle, reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) == 0;
  closesocket(socketHandle);
  return canBind;
#else
  (void)port;
  return false;
#endif
}

} // namespace

uint16_t ResolveNetworkPlayBasePort(const NetworkPlayPlannerSettings &settings) {
  const uint16_t preferredPort = static_cast<uint16_t>(
      glm::clamp(static_cast<int>(settings.basePort), 1024, 65535));
  if (!settings.autoAllocatePorts) {
    return preferredPort;
  }

  for (uint port = preferredPort; port < 65535; ++port) {
    if (IsPortBindable(static_cast<uint16_t>(port))) {
      return static_cast<uint16_t>(port);
    }
  }

  return preferredPort;
}

String BuildNetworkPlayLaunchId() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return "network-play-" + std::to_string(millis);
}

String GetNetworkPlayRoleFlag(ToolKitNetworking::HostingMode hostingMode) {
  using ToolKitNetworking::HostingMode;
  switch (hostingMode) {
  case HostingMode::DedicatedServer:
    return "-dedicated";
  case HostingMode::ListenServer:
    return "-host";
  case HostingMode::Client:
    return "-client";
  case HostingMode::None:
  default:
    return {};
  }
}

bool BuildNetworkPlaySessionSpec(const NetworkPlayPlannerSettings &settings,
                                 const NetworkPlaySceneConfig &scene,
                                 const NetworkPlaySessionMetadata &metadata,
                                 NetworkPlaySessionSpec &session,
                                 String &errorMessage) {
  using namespace ToolKitNetworking;

  if (scene.joinMethod != JoinMethod::DirectAddress) {
    errorMessage =
        "Editor network play currently supports DirectAddress only.";
    return false;
  }

  session = {};
  session.launchId = metadata.launchId;
  session.launchRoot = metadata.launchRoot;
  session.projectRoot = metadata.projectRoot;
  session.workspaceRoot = metadata.workspaceRoot;
  session.configTemplateRoot = metadata.configTemplateRoot;
  session.resourceRoot = metadata.resourceRoot;
  session.scenePath = metadata.scenePath;
  session.executablePath = metadata.executablePath;
  session.topology = settings.topology;
  session.basePort = ResolveNetworkPlayBasePort(settings);
  session.playerCount = settings.playerCount > 1
                            ? static_cast<uint>(settings.playerCount)
                            : static_cast<uint>(1);

  const String localLoopback = "127.0.0.1";
  const uint localMaxClients = session.playerCount;

  NetworkPlayInstanceSpec localInstance;
  localInstance.instanceId = "editor-local";
  localInstance.maxClients = localMaxClients;
  localInstance.bindAddress = scene.bindAddress;
  localInstance.advertisedAddress = scene.advertisedAddress;
  localInstance.listenPort = scene.listenPort;
  localInstance.connectHost = scene.connectHost;
  localInstance.connectPort = scene.connectPort;

  switch (settings.topology) {
  case NetworkPlayTopology::ListenServer: {
    localInstance.roleName = "ListenServerHost";
    localInstance.hostingMode = HostingMode::ListenServer;
    localInstance.bindAddress = localLoopback;
    localInstance.advertisedAddress = localLoopback;
    localInstance.listenPort = session.basePort;
    localInstance.connectHost = localLoopback;
    localInstance.connectPort = session.basePort;

    for (uint playerIndex = 1; playerIndex < session.playerCount; ++playerIndex) {
      NetworkPlayInstanceSpec child;
      child.instanceId = "client-" + std::to_string(playerIndex + 1);
      child.roleName = "Client";
      child.hostingMode = HostingMode::Client;
      child.connectHost = localLoopback;
      child.connectPort = session.basePort;
      child.maxClients = localMaxClients;
      session.childInstances.push_back(child);
    }
    break;
  }
  case NetworkPlayTopology::DedicatedServer: {
    if (scene.maxClients < session.playerCount) {
      errorMessage =
          "DedicatedServer editor network play requires the scene "
          "NetworkManager MaxClients to be at least the requested player "
          "count.";
      return false;
    }

    localInstance.roleName = "Client";
    localInstance.hostingMode = HostingMode::Client;
    localInstance.connectHost = localLoopback;
    localInstance.connectPort = session.basePort;

    NetworkPlayInstanceSpec serverChild;
    serverChild.instanceId = "dedicated-server";
    serverChild.roleName = "DedicatedServer";
    serverChild.hostingMode = HostingMode::DedicatedServer;
    serverChild.headless = settings.runDedicatedServerHeadless;
    serverChild.listenPort = session.basePort;
    serverChild.bindAddress = localLoopback;
    serverChild.advertisedAddress = localLoopback;
    serverChild.maxClients = localMaxClients;
    session.childInstances.push_back(serverChild);

    for (uint playerIndex = 1; playerIndex < session.playerCount; ++playerIndex) {
      NetworkPlayInstanceSpec child;
      child.instanceId = "client-" + std::to_string(playerIndex + 1);
      child.roleName = "Client";
      child.hostingMode = HostingMode::Client;
      child.connectHost = localLoopback;
      child.connectPort = session.basePort;
      child.maxClients = localMaxClients;
      session.childInstances.push_back(child);
    }
    break;
  }
  case NetworkPlayTopology::ClientAttach:
  default: {
    if (scene.connectHost.empty() || scene.connectPort == 0) {
      errorMessage =
          "ClientAttach editor network play requires an explicit ConnectHost "
          "and ConnectPort on the scene NetworkManager.";
      return false;
    }

    localInstance.roleName = "Client";
    localInstance.hostingMode = HostingMode::Client;
    localInstance.connectHost = scene.connectHost;
    localInstance.connectPort = scene.connectPort;

    for (uint playerIndex = 1; playerIndex < session.playerCount; ++playerIndex) {
      NetworkPlayInstanceSpec child;
      child.instanceId = "client-" + std::to_string(playerIndex + 1);
      child.roleName = "Client";
      child.hostingMode = HostingMode::Client;
      child.connectHost = localInstance.connectHost;
      child.connectPort = localInstance.connectPort;
      child.maxClients = localMaxClients;
      session.childInstances.push_back(child);
    }
    break;
  }
  }

  session.localInstance = localInstance;
  return true;
}

String BuildNetworkPlayManifestXml(const NetworkPlaySessionSpec &session,
                                   const NetworkPlayInstanceSpec &instance,
                                   const String &configRoot) {
  String xml;
  xml += "<NetworkPlayInstance";
  xml += " launchId=\"" + EscapeXml(session.launchId) + "\"";
  xml += " instanceId=\"" + EscapeXml(instance.instanceId) + "\"";
  xml += " topology=\"" + EscapeXml(TopologyToString(session.topology)) + "\"";
  xml += " role=\"" + EscapeXml(instance.roleName) + "\"";
  xml += " projectRoot=\"" + EscapeXml(session.projectRoot) + "\"";
  xml += " workspaceRoot=\"" + EscapeXml(session.workspaceRoot) + "\"";
  xml += " scenePath=\"" + EscapeXml(session.scenePath) + "\"";
  xml += " configRoot=\"" + EscapeXml(configRoot) + "\"";
  xml += " resourceRoot=\"" + EscapeXml(session.resourceRoot) + "\"";
  xml += " autoPlay=\"1\"";
  xml += " headless=\"" + BoolToString(instance.headless) + "\"";
  xml += " joinMethod=\"DirectAddress\"";
  xml += " connectHost=\"" + EscapeXml(instance.connectHost) + "\"";
  xml += " connectPort=\"" + std::to_string(instance.connectPort) + "\"";
  xml += " listenPort=\"" + std::to_string(instance.listenPort) + "\"";
  xml += " bindAddress=\"" + EscapeXml(instance.bindAddress) + "\"";
  xml += " advertisedAddress=\"" + EscapeXml(instance.advertisedAddress) + "\"";
  xml += " maxClients=\"" + std::to_string(instance.maxClients) + "\"";
  xml += " />";
  return xml;
}

} // namespace ToolKit::Editor
