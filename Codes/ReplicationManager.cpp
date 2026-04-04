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

namespace ToolKit::ToolKitNetworking {
ReplicationManager::ReplicationManager(NetworkManager &owner) : m_owner(owner) {}

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
  m_currentServerTick = 0;
  m_clientUpdateTimer = 0.0f;
  m_sendStream.Clear();
  m_receiveStream.Clear();

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

void ReplicationManager::ReceivePacket(int type, GamePacket *payload, int source) {
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
      }

      if (m_owner.GetPlayerPrefabVal()) {
        SpawnNetworkObject(m_owner.GetPlayerPrefabVal()->GetFile(), source,
                           Vec3(0, 5, 0), Quaternion());
      } else {
        TK_LOG("NetworkManager: No PlayerPrefab configured! New client will "
               "not have a player object.");
      }
    }
  } else if (type == NetworkMessage::Spawn) {
    SpawnPacket *p = (SpawnPacket *)payload;

    if (!FindComponentByNetworkID(p->networkID)) {
      std::string className = p->className;
      EntityPtr newEntity = nullptr;
      NetworkComponent *netComp =
          InstantiateNetworkObject(className, newEntity);

      if (netComp && newEntity) {
        netComp->SetNetworkID(p->networkID);
        netComp->SetOwnerID(p->ownerID);
        netComp->SetSpawnClassName(className);
        netComp->SetIsDynamicallySpawned(true);

        newEntity->m_node->SetTranslation(Vec3(p->px, p->py, p->pz));
        newEntity->m_node->SetOrientation(
            Quaternion(p->rw, p->rx, p->ry, p->rz));

        RegisterComponent(netComp);
        netComp->OnNetworkSpawn();
      } else {
        TK_LOG(("NetworkManager: Client failed to spawn object: " + className)
                   .c_str());
      }
    }
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
