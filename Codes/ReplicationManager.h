#pragma once

#include "NetworkComponent.h"
#include "NetworkPackets.h"
#include "NetworkSessionTypes.h"
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
  bool BeginSessionHandshake(const SessionJoinRequest &request);
  bool IsSessionAuthenticated() const;
  bool HasSessionAuthFailed() const;
  DisconnectReason GetSessionAuthFailureReason() const;
  const String &GetSessionAuthFailureDetail() const;

private:
  struct PeerHandshakeState {
    bool challengeSent = false;
    bool authenticated = false;
    uint64_t clientNonce = 0;
    uint64_t serverNonce = 0;
  };

  NetworkComponent *InstantiateNetworkObject(const std::string &typeOrPath,
                                             EntityPtr &outEntity);
  NetworkComponent *FindComponentByNetworkID(int networkID) const;
  bool IsPeerAuthenticated(int peerID) const;
  void ResetAuthenticationState();
  void RejectPeer(int peerID, DisconnectReason reason,
                  const String &detail) const;
  void RejectLocalSession(DisconnectReason reason, const String &detail);
  void HandleHandshakeHello(HandshakeHelloPacket *packet, int source);
  void HandleHandshakeChallenge(HandshakeChallengePacket *packet);
  void HandleHandshakeResponse(HandshakeResponsePacket *packet, int source);
  void HandleHandshakeAccept(HandshakeAcceptPacket *packet);
  void HandleHandshakeReject(HandshakeRejectPacket *packet);
  void BroadcastSnapshot();
  void SendSnapshotToPeer(int peerID, int baseTick);
  void UpdateAsServer(float deltaTime);
  void UpdateAsClient(float deltaTime);

private:
  NetworkManager &m_owner;
  int m_nextNetworkID = 1;
  std::map<int, int> m_peerLastAckedTick;
  std::map<int, PeerHandshakeState> m_peerHandshakeStates;
  std::vector<NetworkComponent *> m_networkComponents;
  PacketStream m_sendStream;
  PacketStream m_receiveStream;
  int m_currentServerTick = 0;
  float m_clientUpdateTimer = 0.0f;
  bool m_handshakeStarted = false;
  bool m_localSessionAuthenticated = false;
  bool m_localAuthFailed = false;
  uint64_t m_localClientNonce = 0;
  uint64_t m_localServerNonce = 0;
  SessionJoinRequest m_pendingJoinRequest;
  DisconnectReason m_authFailureReason = DisconnectReason::None;
  String m_authFailureDetail;
};
} // namespace ToolKit::ToolKitNetworking
