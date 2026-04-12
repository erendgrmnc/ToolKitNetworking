/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Editor/SimulationWindow.h"
#include "NetworkMacros.h"
#include "NetworkSessionTypes.h"

#include <vector>

namespace ToolKit::Editor {

struct TK_NET_API NetworkPlayPlannerSettings {
  uint playerCount = 2;
  bool runDedicatedServerHeadless = true;
  uint basePort = 7777;
  bool autoAllocatePorts = true;
  NetworkPlayTopology topology = NetworkPlayTopology::ListenServer;
};

struct TK_NET_API NetworkPlayInstanceSpec {
  String instanceId;
  String roleName;
  ToolKitNetworking::HostingMode hostingMode =
      ToolKitNetworking::HostingMode::None;
  bool headless = false;
  String connectHost;
  uint16_t connectPort = 0;
  uint16_t listenPort = 0;
  String bindAddress;
  String advertisedAddress;
  uint maxClients = 0;
};

struct TK_NET_API NetworkPlaySessionSpec {
  String launchId;
  String launchRoot;
  String projectRoot;
  String workspaceRoot;
  String configTemplateRoot;
  String resourceRoot;
  String scenePath;
  String executablePath;
  NetworkPlayTopology topology = NetworkPlayTopology::ListenServer;
  uint16_t basePort = 0;
  uint playerCount = 1;
  NetworkPlayInstanceSpec localInstance;
  std::vector<NetworkPlayInstanceSpec> childInstances;
};

struct TK_NET_API NetworkPlaySceneConfig {
  ToolKitNetworking::JoinMethod joinMethod =
      ToolKitNetworking::JoinMethod::DirectAddress;
  uint maxClients = 0;
  String connectHost;
  uint16_t connectPort = 0;
  uint16_t listenPort = 0;
  String bindAddress;
  String advertisedAddress;
};

struct TK_NET_API NetworkPlaySessionMetadata {
  String launchId;
  String launchRoot;
  String projectRoot;
  String workspaceRoot;
  String configTemplateRoot;
  String resourceRoot;
  String scenePath;
  String executablePath;
};

TK_NET_API uint16_t ResolveNetworkPlayBasePort(
    const NetworkPlayPlannerSettings &settings);

TK_NET_API bool BuildNetworkPlaySessionSpec(
    const NetworkPlayPlannerSettings &settings, const NetworkPlaySceneConfig &scene,
    const NetworkPlaySessionMetadata &metadata, NetworkPlaySessionSpec &session,
    String &errorMessage);

TK_NET_API String BuildNetworkPlayManifestXml(
    const NetworkPlaySessionSpec &session, const NetworkPlayInstanceSpec &instance,
    const String &configRoot);

TK_NET_API String BuildNetworkPlayLaunchId();

TK_NET_API String GetNetworkPlayRoleFlag(
    ToolKitNetworking::HostingMode hostingMode);

} // namespace ToolKit::Editor
