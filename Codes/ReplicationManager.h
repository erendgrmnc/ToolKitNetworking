#pragma once

#include "NetworkComponent.h"
#include "NetworkPackets.h"
#include <map>
#include <vector>

namespace ToolKit::ToolKitNetworking {
class NetworkManager;

class ReplicationManager {
public:
  explicit ReplicationManager(NetworkManager &owner);

  void RegisterComponent(NetworkComponent *networkComponent);
  void UnregisterComponent(NetworkComponent *networkComponent);
  void ClearRegisteredComponents();
  const std::vector<NetworkComponent *> &GetNetworkComponents() const;

  NetworkComponent *SpawnNetworkObject(const std::string &prefabName,
                                       int ownerID, const Vec3 &pos,
                                       const Quaternion &rot);
  void DespawnNetworkObject(NetworkComponent *component);
  void SendClientUpdate(NetworkComponent *component);
  void ReceivePacket(int type, GamePacket *payload, int source);
  void Update(float deltaTime);

  int GetServerTick() const;
  void SetServerTick(int tick);
  void SendRPCPacket(PacketStream &rpcStream, RPCReceiver target, int ownerID);

private:
  NetworkComponent *InstantiateNetworkObject(const std::string &typeOrPath,
                                             EntityPtr &outEntity);
  NetworkComponent *FindComponentByNetworkID(int networkID) const;
  void BroadcastSnapshot();
  void SendSnapshotToPeer(int peerID, int baseTick);
  void UpdateAsServer(float deltaTime);
  void UpdateAsClient(float deltaTime);

private:
  NetworkManager &m_owner;
  int m_nextNetworkID = 1;
  std::map<int, int> m_peerLastAckedTick;
  std::vector<NetworkComponent *> m_networkComponents;
  PacketStream m_sendStream;
  PacketStream m_receiveStream;
  int m_currentServerTick = 0;
  float m_clientUpdateTimer = 0.0f;
};
} // namespace ToolKit::ToolKitNetworking
