#include "NetworkManager.h"
#include "GameClient.h"
#include "GameServer.h"
#include "NetworkSessionManager.h"
#include "NetworkSpawnService.h"
#include <algorithm>
#include <Prefab.h>
#include <Scene.h>
#include <ToolKit.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>
#endif

namespace ToolKit::ToolKitNetworking {
TKDefineClass(NetworkManager, Component);
NetworkManager *NetworkManager::Instance = nullptr;
} // namespace ToolKit::ToolKitNetworking

ToolKit::ToolKitNetworking::NetworkManager::NetworkManager() {
  Instance = this;
  m_server = nullptr;
  m_client = nullptr;
  m_useDeltaCompression = true;
  m_connectHost = "127.0.0.1";
  m_connectPort = 8080;
  m_listenPort = 8080;
  m_bindAddress.clear();
  m_advertisedAddress.clear();
  m_maxClients = 2;
  m_sessionId.clear();
  m_joinCredential.clear();
  m_requireJoinCredential = false;
  m_buildCompatibilityId.clear();
  m_enableInterpolation = true;
  m_enableExtrapolation = false;
  m_enableLagCompensation = false;
  m_bufferTime = 0.1f;

  ToolKit::MultiChoiceVariant roleVar;
  {
    ToolKit::ParameterVariant v((int)NetworkRole::None);
    v.m_name = "None";
    roleVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)NetworkRole::Client);
    v.m_name = "Client";
    roleVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)NetworkRole::DedicatedServer);
    v.m_name = "DedicatedServer";
    roleVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)NetworkRole::Host);
    v.m_name = "Host";
    roleVar.Choices.push_back(v);
  }
  roleVar.CurrentVal.Index = 0;
  m_role = roleVar;

  ToolKit::MultiChoiceVariant presetVar;
  {
    ToolKit::ParameterVariant v((int)MovementPreset::Competitive);
    v.m_name = "Competitive";
    presetVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)MovementPreset::Smooth);
    v.m_name = "Smooth";
    presetVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)MovementPreset::Vehicle);
    v.m_name = "Vehicle";
    presetVar.Choices.push_back(v);
  }
  {
    ToolKit::ParameterVariant v((int)MovementPreset::Custom);
    v.m_name = "Custom";
    presetVar.Choices.push_back(v);
  }
  presetVar.CurrentVal.Index = 1;
  m_preset = presetVar;

  m_replicationManager = std::make_unique<ReplicationManager>(*this);
  m_sessionManager = std::make_unique<NetworkSessionManager>(*this);
  NetworkBase::Initialise();
}

ToolKit::ToolKitNetworking::NetworkManager::~NetworkManager() {
  Stop();
  if (Instance == this) {
    Instance = nullptr;
  }
  NetworkBase::Destroy();
}

bool ToolKit::ToolKitNetworking::NetworkManager::StartConfiguredSession() {
  return m_sessionManager != nullptr && m_sessionManager->StartConfiguredSession();
}

bool ToolKit::ToolKitNetworking::NetworkManager::StartAsClient(
    const std::string &host, int portNum) {
  if (m_client) {
    TK_LOG("Client already running. Stopping previous instance.");
    m_client->Disconnect();
    m_client = nullptr;
  }

  m_client = MakeNewPtr<GameClient>();
  if (ITransportPeer *client = m_client.get()) {
    bool isConnected = client->Connect(host, portNum);
    if (isConnected) {
      client->RegisterPacketHandler(NetworkMessage::HandshakeChallenge, this);
      client->RegisterPacketHandler(NetworkMessage::HandshakeAccept, this);
      client->RegisterPacketHandler(NetworkMessage::HandshakeReject, this);
      client->RegisterPacketHandler(NetworkMessage::Snapshot, this);
      client->RegisterPacketHandler(NetworkMessage::Spawn, this);
      client->RegisterPacketHandler(NetworkMessage::Despawn, this);
      client->RegisterPacketHandler(NetworkMessage::ClientConnected, this);
      client->RegisterPacketHandler(NetworkMessage::Shutdown, this);
      client->RegisterPacketHandler(NetworkMessage::RPC, this);
    } else {
      m_client = nullptr;
    }
    return isConnected;
  }

  return false;
}

bool ToolKit::ToolKitNetworking::NetworkManager::StartAsServer(uint16_t port) {
  if (m_server) {
    TK_LOG("Server already running. Stopping previous instance.");
    m_server->Shutdown();
    m_server = nullptr;
  }

  const SessionHostRequest &hostRequest = GetLastHostRequest();
  const String bindAddress =
      hostRequest.bindEndpoint.host.empty() ? m_bindAddress : hostRequest.bindEndpoint.host;
  const uint maxClients = hostRequest.maxClients == 0 ? m_maxClients : hostRequest.maxClients;

  m_server = MakeNewPtr<GameServer>(bindAddress, port,
                                    static_cast<int>((std::max)(1u, maxClients)));
  if (!m_server || !m_server->IsInitialised()) {
    TK_LOG("Failed to start server transport.");
    m_server = nullptr;
    return false;
  }

  if (m_replicationManager) {
    for (auto *networkComponent : m_replicationManager->GetNetworkComponents()) {
      if (networkComponent->GetOwnerID() == -1) {
        networkComponent->SetOwnerID(0);
      }
    }
  }

  m_server->RegisterPacketHandler(
      ToolKitNetworking::NetworkMessage::HandshakeHello, this);
  m_server->RegisterPacketHandler(
      ToolKitNetworking::NetworkMessage::HandshakeResponse, this);
  m_server->RegisterPacketHandler(
      ToolKitNetworking::NetworkMessage::ClientConnected, this);
  m_server->RegisterPacketHandler(
      ToolKitNetworking::NetworkMessage::PeerDisconnected, this);
  m_server->RegisterPacketHandler(
      ToolKitNetworking::NetworkMessage::SnapshotAck, this);
  m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::RPC, this);
  m_server->RegisterPacketHandler(NetworkMessage::ClientUpdate, this);

  const std::string serverLogStr =
      "Started as server on port " + std::to_string(port);
  TK_LOG(serverLogStr.c_str());

  return true;
}

void ToolKit::ToolKitNetworking::NetworkManager::Stop() {
  StopSessionTransports();

  if (m_sessionManager) {
    m_sessionManager->StopSession();
  }
}

ToolKit::ToolKitNetworking::NetworkSpawnService &
ToolKit::ToolKitNetworking::NetworkManager::GetSpawnService() {
  return NetworkSpawnService::GetInstance();
}

ToolKit::ToolKitNetworking::NetworkComponent *
ToolKit::ToolKitNetworking::NetworkManager::SpawnNetworkObject(
    const std::string &prefabName, int ownerID, const Vec3 &pos,
    const Quaternion &rot) {
  if (!m_replicationManager) {
    return nullptr;
  }

  return m_replicationManager->SpawnNetworkObject(prefabName, ownerID, pos, rot);
}

void ToolKit::ToolKitNetworking::NetworkManager::DespawnNetworkObject(
    NetworkComponent *component) {
  if (m_replicationManager) {
    m_replicationManager->DespawnNetworkObject(component);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::SendClientUpdate(
    NetworkComponent *component) {
  if (m_replicationManager) {
    m_replicationManager->SendClientUpdate(component);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::ReceivePacket(
    int type, GamePacket *payload, int source) {
  if (m_replicationManager) {
    m_replicationManager->ReceivePacket(type, payload, source);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::Update(float deltaTime) {
  if (m_sessionManager) {
    m_sessionManager->Update();
  }

  if (m_replicationManager) {
    m_replicationManager->Update(deltaTime);
  }
}

int ToolKit::ToolKitNetworking::NetworkManager::GetServerTick() const {
  if (m_replicationManager) {
    return m_replicationManager->GetServerTick();
  }

  return 0;
}

void ToolKit::ToolKitNetworking::NetworkManager::SetServerTick(int tick) {
  if (m_replicationManager) {
    m_replicationManager->SetServerTick(tick);
  }
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsServer() const {
  return m_server != nullptr;
}

int ToolKit::ToolKitNetworking::NetworkManager::GetLocalPeerID() const {
  if (m_client) {
    return m_client->GetPeerID();
  }
  if (IsServer()) {
    return 0;
  }
  return -1;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsDedicatedServer() const {
  return m_role.GetEnum<NetworkRole>() == NetworkRole::DedicatedServer;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsHost() const {
  return m_role.GetEnum<NetworkRole>() == NetworkRole::Host;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsClient() const {
  return m_role.GetEnum<NetworkRole>() == NetworkRole::Client;
}

ToolKit::ToolKitNetworking::SessionBootstrapConfig
ToolKit::ToolKitNetworking::NetworkManager::GetSessionBootstrapConfig() const {
  SessionBootstrapConfig config;
  config.hostingMode = GetConfiguredHostingMode();
  config.connectHost = m_connectHost;
  config.connectPort =
      static_cast<uint16_t>((std::min<uint>)(m_connectPort, 65535u));
  config.listenPort =
      static_cast<uint16_t>((std::min<uint>)(m_listenPort, 65535u));
  config.bindAddress = m_bindAddress;
  config.advertisedAddress = m_advertisedAddress;
  config.buildCompatibilityId = m_buildCompatibilityId;
  config.sessionId = m_sessionId;
  config.joinCredential = m_joinCredential;
  config.maxClients = (std::max)(1u, m_maxClients);
  config.requireJoinCredential = m_requireJoinCredential;
  return config;
}

const std::vector<ToolKit::ToolKitNetworking::NetworkComponent *> &
ToolKit::ToolKitNetworking::NetworkManager::GetNetworkComponents() const {
  static const std::vector<NetworkComponent *> emptyComponents;
  if (!m_replicationManager) {
    return emptyComponents;
  }

  return m_replicationManager->GetNetworkComponents();
}

ToolKit::ToolKitNetworking::HostingMode
ToolKit::ToolKitNetworking::NetworkManager::GetConfiguredHostingMode() const {
  return SessionCore::LegacyRoleToHostingMode(m_role.GetEnum<NetworkRole>());
}

bool ToolKit::ToolKitNetworking::NetworkManager::StartServerTransport(
    uint16_t port) {
  return StartAsServer(port);
}

bool ToolKit::ToolKitNetworking::NetworkManager::StartClientTransport(
    const String &host, uint16_t port) {
  return StartAsClient(host, static_cast<int>(port));
}

void ToolKit::ToolKitNetworking::NetworkManager::StopSessionTransports() {
  ShutdownTransports();

  if (m_replicationManager) {
    m_replicationManager->ClearRegisteredComponents();
  }
}

bool ToolKit::ToolKitNetworking::NetworkManager::HasServerTransport() const {
  return m_server != nullptr;
}

bool ToolKit::ToolKitNetworking::NetworkManager::HasClientTransport() const {
  return m_client != nullptr;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsClientTransportConnected() const {
  return m_client != nullptr && m_client->GetIsConnected();
}

bool ToolKit::ToolKitNetworking::NetworkManager::BeginSessionHandshake(
    const SessionJoinRequest &request) {
  return m_replicationManager != nullptr &&
         m_replicationManager->BeginSessionHandshake(request);
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsSessionAuthenticated() const {
  return m_replicationManager != nullptr &&
         m_replicationManager->IsSessionAuthenticated();
}

bool ToolKit::ToolKitNetworking::NetworkManager::HasSessionAuthFailed() const {
  return m_replicationManager != nullptr &&
         m_replicationManager->HasSessionAuthFailed();
}

ToolKit::ToolKitNetworking::DisconnectReason
ToolKit::ToolKitNetworking::NetworkManager::GetSessionAuthFailureReason() const {
  if (!m_replicationManager) {
    return DisconnectReason::None;
  }

  return m_replicationManager->GetSessionAuthFailureReason();
}

ToolKit::String
ToolKit::ToolKitNetworking::NetworkManager::GetSessionAuthFailureDetail() const {
  if (!m_replicationManager) {
    return {};
  }

  return m_replicationManager->GetSessionAuthFailureDetail();
}

void ToolKit::ToolKitNetworking::NetworkManager::SetReplicationClockNowProviderForTests(
    std::function<uint64_t()> clockNowProvider) {
  if (m_replicationManager) {
    m_replicationManager->SetClockNowProvider(std::move(clockNowProvider));
  }
}

ToolKit::ToolKitNetworking::ConnectionStatus
ToolKit::ToolKitNetworking::NetworkManager::GetConnectionStatus() const {
  if (m_sessionManager) {
    return m_sessionManager->GetConnectionStatus();
  }

  ConnectionStatus status;
  status.state = ConnectionState::Idle;
  status.disconnectReason = DisconnectReason::None;
  status.detailMessage = "No session manager.";
  return status;
}

ToolKit::ToolKitNetworking::HostingMode
ToolKit::ToolKitNetworking::NetworkManager::GetHostingMode() const {
  if (m_sessionManager) {
    return m_sessionManager->ResolveHostingMode();
  }

  return HostingMode::None;
}

const ToolKit::ToolKitNetworking::SessionDescriptor &
ToolKit::ToolKitNetworking::NetworkManager::GetActiveSession() const {
  static const SessionDescriptor emptySession;
  if (!m_sessionManager) {
    return emptySession;
  }

  return m_sessionManager->GetActiveSession();
}

const ToolKit::ToolKitNetworking::SessionHostRequest &
ToolKit::ToolKitNetworking::NetworkManager::GetLastHostRequest() const {
  static const SessionHostRequest emptyHostRequest;
  if (!m_sessionManager) {
    return emptyHostRequest;
  }

  return m_sessionManager->GetLastHostRequest();
}

const ToolKit::ToolKitNetworking::SessionJoinRequest &
ToolKit::ToolKitNetworking::NetworkManager::GetLastJoinRequest() const {
  static const SessionJoinRequest emptyJoinRequest;
  if (!m_sessionManager) {
    return emptyJoinRequest;
  }

  return m_sessionManager->GetLastJoinRequest();
}

void ToolKit::ToolKitNetworking::NetworkManager::SendRPCPacket(
    PacketStream &rpcStream, RPCReceiver target, int ownerID) {
  if (m_replicationManager) {
    m_replicationManager->SendRPCPacket(rpcStream, target, ownerID);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::RegisterComponent(
    NetworkComponent *networkComponent) {
  if (m_replicationManager) {
    m_replicationManager->RegisterComponent(networkComponent);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::UnregisterComponent(
    NetworkComponent *networkComponent) {
  if (m_replicationManager) {
    m_replicationManager->UnregisterComponent(networkComponent);
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::ClearRegisteredComponents() {
  if (m_replicationManager) {
    m_replicationManager->ClearRegisteredComponents();
  }
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateMinimumState() {}

void ToolKit::ToolKitNetworking::NetworkManager::ParameterConstructor() {
  Component::ParameterConstructor();

  Role_Define(m_role, NetworkManagerCategory.Name,
              NetworkManagerCategory.Priority, true, true);
  UseDeltaCompression_Define(m_useDeltaCompression, NetworkManagerCategory.Name,
                             NetworkManagerCategory.Priority, true, true);
  ConnectHost_Define(m_connectHost, NetworkManagerCategory.Name,
                     NetworkManagerCategory.Priority, true, true);
  ConnectPort_Define(m_connectPort, NetworkManagerCategory.Name,
                     NetworkManagerCategory.Priority, true, true);
  ListenPort_Define(m_listenPort, NetworkManagerCategory.Name,
                    NetworkManagerCategory.Priority, true, true);
  BindAddress_Define(m_bindAddress, NetworkManagerCategory.Name,
                     NetworkManagerCategory.Priority, true, true);
  AdvertisedAddress_Define(m_advertisedAddress, NetworkManagerCategory.Name,
                           NetworkManagerCategory.Priority, true, true);
  MaxClients_Define(m_maxClients, NetworkManagerCategory.Name,
                    NetworkManagerCategory.Priority, true, true);
  SessionId_Define(m_sessionId, NetworkManagerCategory.Name,
                   NetworkManagerCategory.Priority, true, true);
  JoinCredential_Define(m_joinCredential, NetworkManagerCategory.Name,
                        NetworkManagerCategory.Priority, true, true);
  RequireJoinCredential_Define(m_requireJoinCredential, NetworkManagerCategory.Name,
                               NetworkManagerCategory.Priority, true, true);
  BuildCompatibilityId_Define(m_buildCompatibilityId, NetworkManagerCategory.Name,
                              NetworkManagerCategory.Priority, true, true);
  Preset_Define(m_preset, NetworkManagerCategory.Name, NetworkManagerCategory.Priority,
                true, true);
  EnableInterpolation_Define(m_enableInterpolation, NetworkManagerCategory.Name,
                             NetworkManagerCategory.Priority, true, true);
  EnableExtrapolation_Define(m_enableExtrapolation, NetworkManagerCategory.Name,
                             NetworkManagerCategory.Priority, true, true);
  EnableLagCompensation_Define(m_enableLagCompensation, NetworkManagerCategory.Name,
                               NetworkManagerCategory.Priority, true, true);
  BufferTime_Define(m_bufferTime, NetworkManagerCategory.Name,
                    NetworkManagerCategory.Priority, true, true);

  PlayerPrefab_Define(m_playerPrefab, NetworkManagerCategory.Name,
                      NetworkManagerCategory.Priority, true, true);

  const auto validatePort = [](ToolKit::Value &val, String &msg) -> bool {
    if (uint *port = std::get_if<uint>(&val)) {
      if (*port == 0 || *port > 65535u) {
        msg = "Port must be in the range 1-65535.";
        return false;
      }
    }
    return true;
  };

  ParamConnectPort().m_validator = validatePort;
  ParamListenPort().m_validator = validatePort;

  ParamMaxClients().m_validator = [](ToolKit::Value &val, String &msg) -> bool {
    if (uint *maxClients = std::get_if<uint>(&val)) {
      if (*maxClients == 0) {
        msg = "Max clients must be at least 1.";
        return false;
      }
    }
    return true;
  };

  ParamPlayerPrefab().m_validator = [](ToolKit::Value &val,
                                       String &msg) -> bool {
    if (ScenePtr *scenePtr = std::get_if<ScenePtr>(&val)) {
      ScenePtr scene = *scenePtr;
      if (scene) {
        String path = scene->GetFile();
        if (path.empty()) {
          return true;
        }

        PrefabPtr prefab = std::make_shared<Prefab>();
        prefab->SetPrefabPathVal(path);
        prefab->Load();

        bool valid = false;
        prefab->Init(ToolKit::SceneWeakPtr());
        for (EntityPtr child : prefab->GetInstancedEntities()) {
          if (child->GetComponent<NetworkComponent>()) {
            valid = true;
            break;
          }
        }
        prefab->UnInit();

        if (!valid) {
          msg = "Prefab must have a NetworkComponent attached to one of its "
                "entities.";
          return false;
        }
      }
    }
    return true;
  };
}

const char *ToolKit::ToolKitNetworking::NetworkManager::GetIPV4() {
  static char ipBuffer[64];
  ipBuffer[0] = '\0';

#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);

  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) != SOCKET_ERROR) {
    addrinfo hints = {}, *info = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, nullptr, &hints, &info) == 0) {
      for (addrinfo *p = info; p != nullptr; p = p->ai_next) {
        sockaddr_in *addr = reinterpret_cast<sockaddr_in *>(p->ai_addr);
        const char *addrStr = inet_ntoa(addr->sin_addr);

        if (strcmp(addrStr, "127.0.0.1") != 0) {
          strncpy(ipBuffer, addrStr, sizeof(ipBuffer) - 1);
          break;
        }
      }

      freeaddrinfo(info);
    }
  }

  WSACleanup();
#else
  struct ifaddrs *ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == 0) {
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
        continue;
      }

      sockaddr_in *addr = reinterpret_cast<sockaddr_in *>(ifa->ifa_addr);
      const char *addrStr =
          inet_ntop(AF_INET, &addr->sin_addr, ipBuffer, sizeof(ipBuffer));

      if (!addrStr) {
        continue;
      }

      if (strcmp(ipBuffer, "127.0.0.1") != 0) {
        break;
      }
    }

    freeifaddrs(ifaddr);
  }
#endif

  if (ipBuffer[0] == '\0') {
    strcpy(ipBuffer, "127.0.0.1");
  }

  return ipBuffer;
}

void ToolKit::ToolKitNetworking::NetworkManager::ShutdownTransports() {
  if (m_server) {
    m_server->Shutdown();
    m_server = nullptr;
  }

  if (m_client) {
    m_client->Disconnect();
    m_client = nullptr;
  }
}

ToolKit::ComponentPtr
ToolKit::ToolKitNetworking::NetworkManager::Copy(EntityPtr entityPtr) {
  NetworkManagerPtr nc = MakeNewPtr<NetworkManager>();
  nc->m_localData = m_localData;
  nc->m_entity = entityPtr;
  return nc;
}
