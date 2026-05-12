#include "ReplicationManager.h"
#include "GameClient.h"
#include "GameServer.h"
#include "NetworkManager.h"
#include "NetworkSpawnService.h"
#include <Entity.h>
#include <Node.h>
#include <Prefab.h>
#include <Scene.h>
#include <ToolKit.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

namespace {
constexpr size_t SessionStringCapacity = 64;
constexpr size_t RejectDetailCapacity = 128;

template <size_t N>
ToolKit::String PacketStringToString(const char (&value)[N]) {
  return ToolKit::String(value, strnlen(value, N));
}

template <size_t N>
void CopyStringToPacketField(char (&target)[N], const ToolKit::String &value) {
  std::memset(target, 0, N);
  strncpy(target, value.c_str(), N - 1);
}

uint64_t GenerateNonce() {
  static std::mt19937_64 generator(std::random_device{}());
  return generator();
}
} // namespace

namespace ToolKit::ToolKitNetworking {
ReplicationManager::ReplicationManager(NetworkManager &owner) : m_owner(owner) {
  m_clockNowProvider = []() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
  };
}

void ReplicationManager::RegisterComponent(NetworkComponent *networkComponent) {
  if (networkComponent->GetNetworkID() == -1) {
    networkComponent->SetNetworkID(m_nextNetworkID++);
  } else if (networkComponent->GetNetworkID() >= m_nextNetworkID) {
    m_nextNetworkID = networkComponent->GetNetworkID() + 1;
  }

  if (std::find(m_networkComponents.begin(), m_networkComponents.end(),
                networkComponent) != m_networkComponents.end()) {
    return;
  }

  if (m_owner.IsServer() && networkComponent->GetOwnerID() == -1) {
    networkComponent->SetOwnerID(0);
  }

  m_networkComponents.push_back(networkComponent);

  std::string logMsg =
      "NetworkComponent Registered: ID " +
      std::to_string(networkComponent->GetNetworkID()) +
      " spawnClass=" + networkComponent->GetSpawnClassName() +
      " isDynamic=" + std::to_string(networkComponent->IsDynamicallySpawned());
  TK_LOG(logMsg.c_str());
}

void ReplicationManager::UnregisterComponent(NetworkComponent *networkComponent) {
  auto it = std::remove(m_networkComponents.begin(), m_networkComponents.end(),
                        networkComponent);
  if (it != m_networkComponents.end()) {
    m_networkComponents.erase(it, m_networkComponents.end());
  }
}

void ReplicationManager::ClearRegisteredComponents() {
  std::vector<NetworkComponent *> toDestroy;
  std::swap(toDestroy, m_networkComponents);
  m_nextNetworkID = 1;
  m_peerLastAckedTick.clear();
  m_peerHandshakeStates.clear();
  m_currentServerTick = 0;
  m_clientUpdateTimer = 0.0f;
  m_sendStream.Clear();
  m_receiveStream.Clear();
  ResetAuthenticationState();

  std::vector<NetworkComponent *> preservedComponents;
  if (GetSceneManager()->GetCurrentScene()) {
    for (auto *nc : toDestroy) {
      if (nc == nullptr) {
        continue;
      }

      if (!nc->IsDynamicallySpawned()) {
        preservedComponents.push_back(nc);
        if (nc->GetNetworkID() >= m_nextNetworkID) {
          m_nextNetworkID = nc->GetNetworkID() + 1;
        }
        continue;
      }

      nc->OnNetworkDespawn();
      if (auto ent = nc->GetEntity()) {
        if (ToolKit::Entity *prefabRoot = ent->GetPrefabRoot()) {
          if (ToolKit::Prefab *prefab = prefabRoot->As<ToolKit::Prefab>()) {
            prefab->UnInit();
          }
          GetSceneManager()->GetCurrentScene()->RemoveEntity(
              prefabRoot->GetIdVal());
        } else {
          GetSceneManager()->GetCurrentScene()->RemoveEntity(ent->GetIdVal());
        }
      }
    }
  } else {
    for (auto *nc : toDestroy) {
      if (nc != nullptr && !nc->IsDynamicallySpawned()) {
        preservedComponents.push_back(nc);
        if (nc->GetNetworkID() >= m_nextNetworkID) {
          m_nextNetworkID = nc->GetNetworkID() + 1;
        }
      }
    }
  }

  m_networkComponents = std::move(preservedComponents);
}

const std::vector<NetworkComponent *> &
ReplicationManager::GetNetworkComponents() const {
  return m_networkComponents;
}

NetworkComponent *ReplicationManager::InstantiateNetworkObject(
    const std::string &typeOrPath, EntityPtr &outEntity) {
  NetworkComponent *netComp = NetworkManager::GetSpawnService().Spawn(typeOrPath);
  outEntity = nullptr;

  if (netComp) {
    outEntity = std::make_shared<Entity>();
    outEntity->AddComponent(ComponentPtr(netComp));
    if (GetSceneManager()->GetCurrentScene()) {
      GetSceneManager()->GetCurrentScene()->AddEntity(outEntity);
    }
    netComp->SetIsDynamicallySpawned(true);
    return netComp;
  }

  String fullPath = typeOrPath;
  if (!ToolKit::CheckFile(fullPath)) {
    fullPath = ToolKit::PrefabPath(fullPath);
  }

  if (auto scene = GetSceneManager()->GetCurrentScene()) {
    int countBefore = (int)scene->GetEntities().size();
    auto &prefab = scene->LinkPrefab(fullPath);
    int countAfter = (int)scene->GetEntities().size();

    if (prefab && !prefab->GetInstancedEntities().empty() &&
        countAfter > countBefore) {
      auto networkEntity = prefab->GetInstancedEntities().front();
      if (networkEntity) {
        if (auto networkComp = networkEntity->GetComponent<NetworkComponent>()) {
          outEntity = networkEntity;
          networkComp->SetIsDynamicallySpawned(true);
          return networkComp.get();
        }
      }
    }
  }

  TK_LOG(("NetworkManager: InstantiateNetworkObject failed for: " + typeOrPath)
             .c_str());
  return nullptr;
}

bool ReplicationManager::IsPeerAuthenticated(int peerID) const {
  auto it = m_peerHandshakeStates.find(peerID);
  return it != m_peerHandshakeStates.end() && it->second.gate.authenticated;
}

uint64_t ReplicationManager::GetNowMs() const {
  return m_clockNowProvider ? m_clockNowProvider() : 0;
}

size_t ReplicationManager::GetPendingHandshakeCount() const {
  size_t count = 0;
  for (const auto &[peerId, state] : m_peerHandshakeStates) {
    (void)peerId;
    if (HandshakeSecurity::HasPendingChallenge(state.gate)) {
      ++count;
    }
  }

  return count;
}

void ReplicationManager::ResetAuthenticationState() {
  m_handshakeStarted = false;
  m_localSessionAuthenticated = false;
  m_localAuthFailed = false;
  m_pendingPreAuthSpawns.clear();
  m_localClientNonce = 0;
  m_localServerNonce = 0;
  m_pendingJoinRequest = SessionJoinRequest{};
  m_authFailureReason = DisconnectReason::None;
  m_authFailureDetail.clear();
}

bool ReplicationManager::RejectPeerWithTracking(int peerID,
                                                DisconnectReason reason,
                                                const String &detail) {
  auto &state = m_peerHandshakeStates[peerID].gate;
  HandshakeSecurity::RecordInvalidAttempt(state, GetNowMs());
  if (HandshakeSecurity::IsPeerBlocked(state, GetNowMs())) {
    RejectPeer(peerID, DisconnectReason::RateLimited,
               "Handshake peer is temporarily rate limited.");
    return false;
  }

  RejectPeer(peerID, reason, detail);
  return true;
}

void ReplicationManager::RejectPeer(int peerID, DisconnectReason reason,
                                    const String &detail) const {
  if (!m_owner.m_server) {
    return;
  }

  HandshakeRejectPacket reject;
  reject.reason = static_cast<int>(reason);
  CopyStringToPacketField(reject.detail, detail);
  m_owner.m_server->SendPacketToPeer(peerID, reject, true);
}

void ReplicationManager::RejectLocalSession(DisconnectReason reason,
                                            const String &detail) {
  m_handshakeStarted = false;
  m_localAuthFailed = true;
  m_localSessionAuthenticated = false;
  m_authFailureReason = reason;
  m_authFailureDetail = detail;
}

NetworkComponent *ReplicationManager::SpawnNetworkObject(
    const std::string &prefabName, int ownerID, const Vec3 &pos,
    const Quaternion &rot) {
  EntityPtr newEntity = nullptr;
  NetworkComponent *netComp = InstantiateNetworkObject(prefabName, newEntity);

  if (!netComp || !newEntity) {
    TK_LOG(("NetworkManager: Failed to spawn - Class factory not found/failed "
            "or Prefab invalid for type: " +
            prefabName)
               .c_str());
    return nullptr;
  }

  newEntity->m_node->SetTranslation(pos);
  newEntity->m_node->SetOrientation(rot);

  if (netComp->GetNetworkID() == -1) {
    netComp->SetNetworkID(m_nextNetworkID++);
  }
  netComp->SetOwnerID(ownerID);

  if (netComp->GetSpawnClassName().empty()) {
    netComp->SetSpawnClassName(prefabName);
  }

  RegisterComponent(netComp);

  netComp->OnNetworkSpawn();

  if (m_owner.IsServer() && m_owner.m_server) {
    SpawnPacket packet;
    packet.networkID = netComp->GetNetworkID();
    packet.ownerID = ownerID;
    packet.px = pos.x;
    packet.py = pos.y;
    packet.pz = pos.z;
    packet.rx = rot.x;
    packet.ry = rot.y;
    packet.rz = rot.z;
    packet.rw = rot.w;
    strncpy(packet.className, prefabName.c_str(), 127);

    TK_LOG(("Replication server broadcasting spawn netID=" +
            std::to_string(packet.networkID) + " owner=" +
            std::to_string(packet.ownerID) + " class=" + prefabName)
               .c_str());
    m_owner.m_server->SendGlobalPacket(packet, true);
  }

  return netComp;
}

void ReplicationManager::DespawnNetworkObject(NetworkComponent *component) {
  if (!component) {
    return;
  }

  int netID = component->GetNetworkID();

  if (m_owner.IsServer() && m_owner.m_server) {
    DespawnPacket packet;
    packet.networkID = netID;
    m_owner.m_server->SendGlobalPacket(packet, true);
  }

  if (auto entity = component->GetEntity()) {
    if (GetSceneManager()->GetCurrentScene()) {
      GetSceneManager()->GetCurrentScene()->RemoveEntity(
          entity->GetIdVal());
    }
  }
}

void ReplicationManager::SendClientUpdate(NetworkComponent *component) {
  if (!component || !m_owner.m_client) {
    return;
  }

  if (m_owner.IsServer()) {
    return;
  }

  if (auto entity = component->GetEntity()) {
    Vec3 pos = entity->m_node->GetTranslation();
    Quaternion rot = entity->m_node->GetOrientation();

    ClientUpdatePacket packet;
    packet.networkID = component->GetNetworkID();
    packet.px = pos.x;
    packet.py = pos.y;
    packet.pz = pos.z;
    packet.rx = rot.x;
    packet.ry = rot.y;
    packet.rz = rot.z;
    packet.rw = rot.w;

    m_owner.m_client->SendPacket(packet, false);
  }
}

NetworkComponent *ReplicationManager::FindComponentByNetworkID(int networkID) const {
  for (auto *networkComponent : m_networkComponents) {
    if (networkComponent->GetNetworkID() == networkID) {
      return networkComponent;
    }
  }

  return nullptr;
}

bool ReplicationManager::BeginSessionHandshake(const SessionJoinRequest &request) {
  if (!m_owner.m_client || !m_owner.m_client->GetIsConnected()) {
    TK_LOG("Replication handshake begin failed: client transport is not connected.");
    RejectLocalSession(DisconnectReason::TransportError,
                       "Transport is not connected for handshake.");
    return false;
  }

  ResetAuthenticationState();
  m_handshakeStarted = true;
  m_pendingJoinRequest = request;
  m_localClientNonce = GenerateNonce();

  HandshakeHelloPacket hello;
  hello.protocolVersion = SessionProtocol::Version;
  hello.requestedHostingMode = static_cast<uint>(HostingMode::Client);
  hello.clientNonce = m_localClientNonce;
  CopyStringToPacketField(hello.sessionId, request.sessionId);
  CopyStringToPacketField(hello.joinCredential, request.joinCredential);
  CopyStringToPacketField(hello.buildCompatibilityId,
                          request.buildCompatibilityId);
  TK_LOG(("Replication client sending HandshakeHello session=" +
          request.sessionId + " target=" + request.targetEndpoint.host + ":" +
          std::to_string(request.targetEndpoint.port))
             .c_str());
  m_owner.m_client->SendPacket(hello, true);
  return true;
}

bool ReplicationManager::IsSessionAuthenticated() const {
  return m_localSessionAuthenticated && m_owner.m_client &&
         m_owner.m_client->GetIsConnected();
}

bool ReplicationManager::HasSessionAuthFailed() const { return m_localAuthFailed; }

DisconnectReason ReplicationManager::GetSessionAuthFailureReason() const {
  return m_authFailureReason;
}

const String &ReplicationManager::GetSessionAuthFailureDetail() const {
  return m_authFailureDetail;
}

void ReplicationManager::SetClockNowProvider(
    std::function<uint64_t()> clockNowProvider) {
  m_clockNowProvider = std::move(clockNowProvider);
}

void ReplicationManager::HandleHandshakeHello(HandshakeHelloPacket *packet,
                                              int source) {
  if (!m_owner.IsServer() || !m_owner.m_server) {
    return;
  }

  TK_LOG(("Replication server received HandshakeHello from peer=" +
          std::to_string(source))
             .c_str());
  PeerHandshakeState &state = m_peerHandshakeStates[source];
  if (HandshakeSecurity::IsPeerBlocked(state.gate, GetNowMs())) {
    RejectPeer(source, DisconnectReason::RateLimited,
               "Handshake peer is temporarily rate limited.");
    return;
  }

  if (packet->protocolVersion != SessionProtocol::Version) {
    RejectPeerWithTracking(source, DisconnectReason::VersionMismatch,
                           "Protocol version mismatch.");
    return;
  }

  const SessionHostRequest &hostRequest = m_owner.GetLastHostRequest();
  const String buildCompatibilityId = PacketStringToString(packet->buildCompatibilityId);
  if (!hostRequest.buildCompatibilityId.empty() &&
      buildCompatibilityId != hostRequest.buildCompatibilityId) {
    RejectPeerWithTracking(source, DisconnectReason::VersionMismatch,
                           "Build compatibility mismatch.");
    return;
  }

  const String sessionId = PacketStringToString(packet->sessionId);
  if (!hostRequest.sessionId.empty() && sessionId != hostRequest.sessionId) {
    RejectPeerWithTracking(source, DisconnectReason::SessionClosed,
                           "Session identifier mismatch.");
    return;
  }

  if (hostRequest.requireJoinCredential) {
    const String joinCredential = PacketStringToString(packet->joinCredential);
    if (joinCredential.empty() || joinCredential != hostRequest.joinCredential) {
      RejectPeerWithTracking(source, DisconnectReason::AuthRejected,
                             "Join credential rejected.");
      return;
    }
  }

  if (packet->requestedHostingMode != static_cast<uint>(HostingMode::Client)) {
    RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                           "Client requested incompatible hosting mode.");
    return;
  }

  if (HandshakeSecurity::IsDuplicateOrStaleHello(state.gate)) {
    RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                           "Duplicate or stale handshake hello.");
    return;
  }

  const size_t pendingLimit =
      static_cast<size_t>((std::max)(1u, m_owner.GetLastHostRequest().maxClients));
  if (!HandshakeSecurity::HasPendingChallenge(state.gate) &&
      GetPendingHandshakeCount() >= pendingLimit) {
    RejectPeerWithTracking(source, DisconnectReason::RateLimited,
                           "Server has reached the pending handshake limit.");
    return;
  }

  HandshakeSecurity::ResetChallenge(state.gate);
  state.gate.authenticated = false;
  state.gate.challengeSent = true;
  state.gate.clientNonce = packet->clientNonce;
  state.gate.serverNonce = GenerateNonce();

  HandshakeChallengePacket challenge;
  challenge.clientNonce = state.gate.clientNonce;
  challenge.serverNonce = state.gate.serverNonce;
  TK_LOG(("Replication server sending HandshakeChallenge to peer=" +
          std::to_string(source))
             .c_str());
  m_owner.m_server->SendPacketToPeer(source, challenge, true);
}

void ReplicationManager::HandleHandshakeChallenge(HandshakeChallengePacket *packet) {
  if (!m_owner.m_client || !m_handshakeStarted || m_localAuthFailed ||
      m_localSessionAuthenticated) {
    return;
  }

  if (packet->clientNonce != m_localClientNonce) {
    RejectLocalSession(DisconnectReason::ProtocolError,
                       "Handshake challenge nonce mismatch.");
    return;
  }

  m_localServerNonce = packet->serverNonce;

  HandshakeResponsePacket response;
  response.clientNonce = m_localClientNonce;
  response.serverNonce = m_localServerNonce;
  TK_LOG("Replication client received HandshakeChallenge; sending HandshakeResponse.");
  m_owner.m_client->SendPacket(response, true);
}

void ReplicationManager::HandleHandshakeResponse(HandshakeResponsePacket *packet,
                                                 int source) {
  if (!m_owner.IsServer() || !m_owner.m_server) {
    return;
  }

  auto it = m_peerHandshakeStates.find(source);
  if (it == m_peerHandshakeStates.end()) {
    RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                           "Unexpected handshake response.");
    return;
  }

  PeerHandshakeState &state = it->second;
  if (HandshakeSecurity::IsPeerBlocked(state.gate, GetNowMs())) {
    RejectPeer(source, DisconnectReason::RateLimited,
               "Handshake peer is temporarily rate limited.");
    return;
  }

  if (HandshakeSecurity::IsDuplicateOrStaleResponse(state.gate)) {
    RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                           "Duplicate or stale handshake response.");
    return;
  }

  if (packet->clientNonce != state.gate.clientNonce ||
      packet->serverNonce != state.gate.serverNonce) {
    RejectPeerWithTracking(source, DisconnectReason::AuthRejected,
                           "Handshake nonce validation failed.");
    HandshakeSecurity::ResetChallenge(state.gate);
    return;
  }

  state.gate.challengeConsumed = true;
  state.gate.authenticated = true;
  m_owner.m_server->AddPeer(source);
  TK_LOG(("Replication server accepted handshake for peer=" +
          std::to_string(source))
             .c_str());

  HandshakeAcceptPacket accept;
  accept.assignedPeerID = source;
  CopyStringToPacketField(accept.sessionId, m_owner.GetActiveSession().sessionId);
  CopyStringToPacketField(accept.buildCompatibilityId,
                          m_owner.GetActiveSession().buildCompatibilityId);
  m_owner.m_server->SendPacketToPeer(source, accept, true);
  TK_LOG(("Replication server sent HandshakeAccept to peer=" +
          std::to_string(source))
             .c_str());

  GamePacket connectedPacket;
  connectedPacket.type = NetworkMessage::ClientConnected;
  ReceivePacket(connectedPacket.type, &connectedPacket, source);
}

void ReplicationManager::HandleHandshakeAccept(HandshakeAcceptPacket *packet) {
  if (!m_owner.m_client || !m_handshakeStarted || m_localAuthFailed) {
    return;
  }

  const String acceptedSessionId = PacketStringToString(packet->sessionId);
  if (!m_pendingJoinRequest.sessionId.empty() &&
      acceptedSessionId != m_pendingJoinRequest.sessionId) {
    RejectLocalSession(DisconnectReason::SessionClosed,
                       "Server accepted a different session identifier.");
    m_owner.m_client->Disconnect();
    return;
  }

  const String acceptedBuildCompatibility =
      PacketStringToString(packet->buildCompatibilityId);
  if (!m_pendingJoinRequest.buildCompatibilityId.empty() &&
      acceptedBuildCompatibility != m_pendingJoinRequest.buildCompatibilityId) {
    RejectLocalSession(DisconnectReason::VersionMismatch,
                       "Server accepted with mismatched build compatibility.");
    m_owner.m_client->Disconnect();
    return;
  }

  m_owner.m_client->SetPeerID(packet->assignedPeerID);
  m_handshakeStarted = false;
  m_localSessionAuthenticated = true;
  m_localAuthFailed = false;
  m_authFailureReason = DisconnectReason::None;
  m_authFailureDetail.clear();
  TK_LOG(("Replication client accepted session; assigned peer=" +
          std::to_string(packet->assignedPeerID))
             .c_str());

  if (!m_pendingPreAuthSpawns.empty()) {
    TK_LOG(("Replication client replaying queued pre-auth spawns count=" +
            std::to_string(m_pendingPreAuthSpawns.size()))
               .c_str());
    std::vector<SpawnPacket> pendingSpawns = std::move(m_pendingPreAuthSpawns);
    m_pendingPreAuthSpawns.clear();
    for (const SpawnPacket &spawn : pendingSpawns) {
      HandleSpawnPacket(spawn);
    }
  }
}

void ReplicationManager::HandleHandshakeReject(HandshakeRejectPacket *packet) {
  TK_LOG(("Replication handshake rejected: " +
          PacketStringToString(packet->detail))
             .c_str());
  RejectLocalSession(static_cast<DisconnectReason>(packet->reason),
                     PacketStringToString(packet->detail));
  if (m_owner.m_client) {
    m_owner.m_client->Disconnect();
  }
}

void ReplicationManager::HandleSpawnPacket(const SpawnPacket &packet) {
  TK_LOG(("Replication client received Spawn netID=" +
          std::to_string(packet.networkID) + " owner=" +
          std::to_string(packet.ownerID) + " class=" + packet.className)
             .c_str());

  if (!FindComponentByNetworkID(packet.networkID)) {
    std::string className = packet.className;
    EntityPtr newEntity = nullptr;
    NetworkComponent *netComp =
        InstantiateNetworkObject(className, newEntity);

    if (netComp && newEntity) {
      netComp->SetNetworkID(packet.networkID);
      netComp->SetOwnerID(packet.ownerID);
      netComp->SetSpawnClassName(className);
      netComp->SetIsDynamicallySpawned(true);

      newEntity->m_node->SetTranslation(
          Vec3(packet.px, packet.py, packet.pz));
      newEntity->m_node->SetOrientation(
          Quaternion(packet.rw, packet.rx, packet.ry, packet.rz));

      RegisterComponent(netComp);
      netComp->OnNetworkSpawn();
      TK_LOG(("Replication client spawned object netID=" +
              std::to_string(packet.networkID) + " owner=" +
              std::to_string(packet.ownerID) + " class=" + className)
                 .c_str());
    } else {
      TK_LOG(("NetworkManager: Client failed to spawn object: " + className)
                 .c_str());
    }
  } else {
    TK_LOG(("Replication client ignored duplicate Spawn netID=" +
            std::to_string(packet.networkID))
               .c_str());
  }
}

void ReplicationManager::ReceivePacket(int type, GamePacket *payload, int source) {
  if (!HandshakeSecurity::HasExpectedFixedPacketSize(type, payload)) {
    if (m_owner.IsServer() && source > 0) {
      RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                             "Malformed handshake packet size.");
    } else {
      RejectLocalSession(DisconnectReason::ProtocolError,
                         "Malformed handshake packet size.");
    }
    return;
  }

  if (type == NetworkMessage::PeerDisconnected) {
    m_peerHandshakeStates.erase(source);
    m_peerLastAckedTick.erase(source);
    return;
  }

  if (type == NetworkMessage::HandshakeHello) {
    HandleHandshakeHello((HandshakeHelloPacket *)payload, source);
    return;
  }

  if (type == NetworkMessage::HandshakeChallenge) {
    HandleHandshakeChallenge((HandshakeChallengePacket *)payload);
    return;
  }

  if (type == NetworkMessage::HandshakeResponse) {
    HandleHandshakeResponse((HandshakeResponsePacket *)payload, source);
    return;
  }

  if (type == NetworkMessage::HandshakeAccept) {
    HandleHandshakeAccept((HandshakeAcceptPacket *)payload);
    return;
  }

  if (type == NetworkMessage::HandshakeReject) {
    HandleHandshakeReject((HandshakeRejectPacket *)payload);
    return;
  }

  if (!HandshakeSecurity::IsAllowedPreAuthMessage(type)) {
    if (m_owner.IsServer() && source > 0 && !IsPeerAuthenticated(source)) {
      RejectPeerWithTracking(source, DisconnectReason::ProtocolError,
                             "Pre-auth packet type is not allowed.");
      return;
    }

    if (!m_owner.IsServer() && m_owner.m_client && !m_localSessionAuthenticated &&
        type != NetworkMessage::Shutdown) {
      if (type == NetworkMessage::Spawn && m_handshakeStarted &&
          !m_localAuthFailed) {
        SpawnPacket *spawn = static_cast<SpawnPacket *>(payload);
        m_pendingPreAuthSpawns.push_back(*spawn);
        TK_LOG(("Replication client queued pre-auth Spawn netID=" +
                std::to_string(spawn->networkID) + " owner=" +
                std::to_string(spawn->ownerID))
                   .c_str());
        return;
      }

      RejectLocalSession(DisconnectReason::ProtocolError,
                         "Received non-handshake packet before authentication.");
      return;
    }
  }

  if (m_owner.IsServer() && source > 0 && !IsPeerAuthenticated(source)) {
    RejectPeer(source, DisconnectReason::AuthRejected,
               "Peer is not authenticated for replication traffic.");
    return;
  }

  if (!m_owner.IsServer() && m_owner.m_client && !m_localSessionAuthenticated &&
      type != NetworkMessage::Shutdown) {
    if (type == NetworkMessage::Spawn && m_handshakeStarted &&
        !m_localAuthFailed) {
      SpawnPacket *spawn = static_cast<SpawnPacket *>(payload);
      m_pendingPreAuthSpawns.push_back(*spawn);
      TK_LOG(("Replication client queued pre-auth Spawn netID=" +
              std::to_string(spawn->networkID) + " owner=" +
              std::to_string(spawn->ownerID))
                 .c_str());
      return;
    }

    RejectLocalSession(DisconnectReason::AuthRejected,
                       "Received replication packet before authentication.");
    return;
  }

  if (type == NetworkMessage::Snapshot) {
    m_receiveStream.Clear();

    int totalSize = payload->GetTotalSize();
    m_receiveStream.Write((void *)payload, totalSize);

    m_receiveStream.readOffset = sizeof(WorldSnapshotPacket);
    WorldSnapshotPacket *packet = (WorldSnapshotPacket *)payload;

    int entityCount = packet->entityCount;
    int baseTick = packet->baseTick;

    if (!m_owner.IsServer()) {
      SetServerTick(packet->serverTick);
    }

    for (int i = 0; i < entityCount; i++) {
      int networkID = -1;
      if (!m_receiveStream.Read(networkID)) {
        break;
      }

      int packetSize = 0;
      if (!m_receiveStream.Read(packetSize)) {
        break;
      }

      if (!m_receiveStream.CanReadSize(static_cast<size_t>(
              packetSize < 0 ? 0 : packetSize)) ||
          packetSize < 0) {
        TK_LOG("Snapshot packet contains invalid component payload size.");
        break;
      }

      NetworkComponent *targetComponent = FindComponentByNetworkID(networkID);
      if (targetComponent) {
        bool isLocallyOwned =
            !m_owner.IsServer() &&
            (targetComponent->GetOwnerID() == m_owner.GetLocalPeerID());

        if (!isLocallyOwned) {
          PacketStream componentStream;
          componentStream.Write(m_receiveStream.buffer.data() +
                                    m_receiveStream.readOffset,
                                static_cast<size_t>(packetSize));
          targetComponent->Deserialize(componentStream, baseTick);
        } else {
          TK_LOG(("Snapshot skipped for locally-owned component: " +
                  std::to_string(networkID))
                     .c_str());
        }
      }

      if (!m_receiveStream.SkipChecked(packetSize)) {
        TK_LOG("Snapshot packet overflow while advancing component payload.");
        break;
      }
    }

    if (m_owner.m_client) {
      SnapshotAckPacket ack;
      ack.ackTick = packet->serverTick;
      m_owner.m_client->SendPacket(ack);
    }
  } else if (type == NetworkMessage::SnapshotAck) {
    SnapshotAckPacket *ack = (SnapshotAckPacket *)payload;
    m_peerLastAckedTick[source] = ack->ackTick;
  } else if (type == NetworkMessage::ClientConnected) {
    if (m_owner.IsServer() && m_owner.m_server) {
      TK_LOG(("Replication server handling ClientConnected for peer=" +
              std::to_string(source) + " existingComponents=" +
              std::to_string(m_networkComponents.size()))
                 .c_str());
      for (auto *nc : m_networkComponents) {
        SpawnPacket msg;
        msg.networkID = nc->GetNetworkID();
        msg.ownerID = nc->GetOwnerID();

        if (nc->GetSpawnClassName().empty()) {
          TK_LOG("Warning: Replicating object with no SpanwClassName!");
          continue;
        }

        strncpy(msg.className, nc->GetSpawnClassName().c_str(), 127);

        if (auto ent = nc->GetEntity()) {
          if (ent->m_scene.lock() == nullptr) {
            continue;
          }

          Vec3 p = ent->m_node->GetTranslation();
          Quaternion r = ent->m_node->GetOrientation();
          msg.px = p.x;
          msg.py = p.y;
          msg.pz = p.z;
          msg.rx = r.x;
          msg.ry = r.y;
          msg.rz = r.z;
          msg.rw = r.w;
        } else {
          continue;
        }

        m_owner.m_server->SendPacketToPeer(source, msg, true);
        TK_LOG(("Replication server replayed spawn to peer=" +
                std::to_string(source) + " netID=" +
                std::to_string(msg.networkID) + " owner=" +
                std::to_string(msg.ownerID) + " class=" + msg.className)
                   .c_str());
      }

      if (m_owner.GetPlayerPrefabVal()) {
        TK_LOG(("Replication server spawning player prefab for peer=" +
                std::to_string(source) + " prefab=" +
                m_owner.GetPlayerPrefabVal()->GetFile())
                   .c_str());
        SpawnNetworkObject(m_owner.GetPlayerPrefabVal()->GetFile(), source,
                           Vec3(0, 5, 0), Quaternion());
      } else {
        TK_LOG("NetworkManager: No PlayerPrefab configured! New client will "
               "not have a player object.");
      }
    }
  } else if (type == NetworkMessage::Spawn) {
    HandleSpawnPacket(*static_cast<SpawnPacket *>(payload));
  } else if (type == NetworkMessage::Despawn) {
    DespawnPacket *p = (DespawnPacket *)payload;
    if (NetworkComponent *target = FindComponentByNetworkID(p->networkID)) {
      if (auto ent = target->GetEntity()) {
        if (GetSceneManager()->GetCurrentScene()) {
          GetSceneManager()->GetCurrentScene()->RemoveEntity(
              ent->GetIdVal());
        }
      }
    }
  } else if (type == NetworkMessage::ClientUpdate) {
    if (m_owner.IsServer()) {
      ClientUpdatePacket *p = (ClientUpdatePacket *)payload;
      NetworkComponent *target = FindComponentByNetworkID(p->networkID);

      if (target && target->GetOwnerID() == source) {
        if (auto ent = target->GetEntity()) {
          ent->m_node->SetTranslation(Vec3(p->px, p->py, p->pz));
          ent->m_node->SetOrientation(Quaternion(p->rw, p->rx, p->ry, p->rz));
        }
      }
    }
  } else if (type == NetworkMessage::RPC) {
    RPCPacket *packet = (RPCPacket *)payload;

    m_receiveStream.Clear();
    m_receiveStream.Write((void *)payload, payload->GetTotalSize());
    m_receiveStream.readOffset = sizeof(RPCPacket);

    {
      std::string log =
          "RPC Recv: netID=" + std::to_string(packet->networkID) +
          " hash=" + std::to_string(packet->functionHash) +
          " numComponents=" + std::to_string(m_networkComponents.size());
      TK_LOG(log.c_str());
      for (auto *nc : m_networkComponents) {
        TK_LOG(("  - Component ID: " + std::to_string(nc->GetNetworkID()))
                   .c_str());
      }
    }

    NetworkComponent *targetComponent =
        FindComponentByNetworkID(packet->networkID);
    if (targetComponent) {
      if (m_owner.IsServer() && source > 0 &&
          targetComponent->GetOwnerID() != source) {
        TK_LOG(("RPC rejected due to ownership mismatch. netID=" +
                std::to_string(packet->networkID) +
                " owner=" + std::to_string(targetComponent->GetOwnerID()) +
                " source=" + std::to_string(source))
                   .c_str());
        return;
      }

      TK_LOG(("RPC Dispatch: found target, calling HandleRPC hash=" +
              std::to_string(packet->functionHash))
                 .c_str());
      targetComponent->HandleRPC(packet->functionHash, m_receiveStream);
    } else {
      TK_LOG("RPC Dispatch: no target component found!");
    }
  }
}

void ReplicationManager::BroadcastSnapshot() {
  if (!m_owner.m_server) {
    return;
  }

  int currentTick = m_owner.m_server->GetServerTick();

  if (!m_owner.m_useDeltaCompression) {
    m_sendStream.Clear();

    WorldSnapshotPacket header;
    header.type = NetworkMessage::Snapshot;
    header.size = 0;
    header.serverTick = currentTick;
    header.baseTick = -1;
    header.entityCount = (int)m_networkComponents.size();
    m_sendStream.Write(header);

    for (auto *networkComponent : m_networkComponents) {
      networkComponent->Serialize(m_sendStream, -1);

      if (auto ent = networkComponent->GetEntity()) {
        Vec3 pos = ent->m_node->GetTranslation();
        String log = "Server Broadcast NetID: " +
                     std::to_string(networkComponent->GetNetworkID()) +
                     " Pos: " + std::to_string(pos.x) + ", " +
                     std::to_string(pos.y) + ", " + std::to_string(pos.z);
        TK_LOG(log.c_str());
      }
    }

    size_t totalSize = m_sendStream.GetSize();
    WorldSnapshotPacket *packetHeader =
        (WorldSnapshotPacket *)m_sendStream.GetData();
    packetHeader->size = (short)(totalSize - sizeof(GamePacket));

    m_owner.m_server->SendGlobalPacket(
        *reinterpret_cast<GamePacket *>(m_sendStream.GetData()), false);
  } else {
    for (int peerID : m_owner.m_server->GetConnectedPeers()) {
      int baseTick = -1;
      if (m_peerLastAckedTick.count(peerID)) {
        baseTick = m_peerLastAckedTick[peerID];
      }
      SendSnapshotToPeer(peerID, baseTick);
    }
  }
}

void ReplicationManager::SendSnapshotToPeer(int peerID, int baseTick) {
  if (!m_owner.m_server) {
    return;
  }

  m_sendStream.Clear();

  WorldSnapshotPacket header;
  header.type = NetworkMessage::Snapshot;
  header.size = 0;
  header.serverTick = m_owner.m_server->GetServerTick();
  header.baseTick = baseTick;
  header.entityCount = (int)m_networkComponents.size();
  m_sendStream.Write(header);

  for (auto *networkComponent : m_networkComponents) {
    networkComponent->Serialize(m_sendStream, baseTick);

    if (auto ent = networkComponent->GetEntity()) {
      Vec3 pos = ent->m_node->GetTranslation();
      String log =
          "Server Send to Peer(" + std::to_string(peerID) +
          ") NetID: " + std::to_string(networkComponent->GetNetworkID()) +
          " Pos: " + std::to_string(pos.x) + ", " + std::to_string(pos.y) +
          ", " + std::to_string(pos.z);
      TK_LOG(log.c_str());
    }
  }

  size_t totalSize = m_sendStream.GetSize();
  WorldSnapshotPacket *packetHeader =
      (WorldSnapshotPacket *)m_sendStream.GetData();
  packetHeader->size = (short)(totalSize - sizeof(GamePacket));

  m_owner.m_server->SendPacketToPeer(
      peerID, *reinterpret_cast<GamePacket *>(m_sendStream.GetData()), false);
}

void ReplicationManager::UpdateAsServer(float deltaTime) {
  (void)deltaTime;

  if (m_owner.m_server) {
    m_owner.m_server->UpdateServer();
  }

  BroadcastSnapshot();
}

void ReplicationManager::UpdateAsClient(float deltaTime) {
  if (!m_owner.m_client) {
    return;
  }

  m_owner.m_client->UpdateClient();
  if (!m_owner.m_client->GetIsConnected()) {
    if (m_localSessionAuthenticated || m_handshakeStarted) {
      ResetAuthenticationState();
    }
    return;
  }

  m_clientUpdateTimer += deltaTime;
  if (m_clientUpdateTimer >= 0.05f) {
    m_clientUpdateTimer = 0.0f;
    for (auto *nc : m_networkComponents) {
      if (nc->IsLocalPlayer()) {
        SendClientUpdate(nc);
      }
    }
  }
}

void ReplicationManager::Update(float deltaTime) {
  if (m_owner.m_server) {
    UpdateAsServer(deltaTime);
  }

  if (m_owner.m_client) {
    UpdateAsClient(deltaTime);
  }
}

int ReplicationManager::GetServerTick() const {
  if (m_owner.m_server) {
    return m_owner.m_server->GetServerTick();
  }

  return m_currentServerTick;
}

void ReplicationManager::SetServerTick(int tick) { m_currentServerTick = tick; }

void ReplicationManager::SendRPCPacket(PacketStream &rpcStream,
                                       RPCReceiver target, int ownerID) {
  GamePacket *packet = reinterpret_cast<GamePacket *>(rpcStream.GetData());

  TK_LOG(("SendRPCPacket: isServer=" + std::to_string(m_owner.IsServer()) +
          " hasClient=" + std::to_string(m_owner.m_client != nullptr) +
          " target=" + std::to_string((int)target))
             .c_str());

  if (m_owner.IsServer() && m_owner.m_server) {
    if (target == RPCReceiver::Server) {
      ReceivePacket(packet->type, packet, -1);
    } else if (target == RPCReceiver::All) {
      m_owner.m_server->SendGlobalPacket(*packet, true);
      ReceivePacket(packet->type, packet, -1);
    } else if (target == RPCReceiver::Owner) {
      if (m_owner.GetLocalPeerID() == ownerID) {
        ReceivePacket(packet->type, packet, -1);
      } else if (ownerID != -1) {
        m_owner.m_server->SendPacketToPeer(ownerID, *packet, true);
      }
    } else if (target == RPCReceiver::Others) {
      m_owner.m_server->SendGlobalPacket(*packet, true);
    }
  } else if (m_owner.m_client) {
    m_owner.m_client->SendPacket(*packet, true);
  }
}
} // namespace ToolKit::ToolKitNetworking
