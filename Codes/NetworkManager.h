#pragma once
#include "NetworkBase.h"
#include "NetworkComponent.h"
#include "NetworkMacros.h"
#include "NetworkPackets.h"
#include "NetworkSpawnService.h"
#include <Component.h>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ToolKit::ToolKitNetworking {
class GameServer;
class GameClient;
class NetworkComponent;
class NetworkSpawnService; // Forward declaration
enum class RPCReceiver;
enum class NetworkRole { None, Client, DedicatedServer, Host };

enum class MovementPreset { Competitive, Smooth, Vehicle, Custom };

struct NetworkSettings {
  bool enableInterpolation = true;
  bool enableExtrapolation = false;
  bool enableLagCompensation = false;
  float bufferTime = 0.1f; // 100ms
};

typedef std::shared_ptr<class NetworkManager> NetworkManagerPtr;
typedef std::vector<NetworkManagerPtr> NetworkManagerPtrArray;

typedef std::shared_ptr<class GameClient> GameClientPtr;
typedef std::shared_ptr<class GameServer> GameServerPtr;

static VariantCategory NetworkManagerCategory{"NetworkManager", 100};

class TK_NET_API NetworkManager : public Component, public PacketReceiver {
public:
  TKDeclareClass(NetworkManager, Component) NetworkManager();
  virtual ~NetworkManager();

  static NetworkManager *Instance;

  void StartAsClient(const std::string &host, int portNum);
  void StartAsServer(uint16_t port);
  void Stop();

  static NetworkSpawnService &GetSpawnService();

  template <typename T> static void RegisterSpawnFactory() {
    GetSpawnService().Register<T>();
  }

  NetworkComponent *SpawnNetworkObject(const std::string &prefabName,
                                       int ownerID, const Vec3 &pos,
                                       const Quaternion &rot);
  void DespawnNetworkObject(NetworkComponent *component);

  void SendClientUpdate(NetworkComponent *component);

  void ReceivePacket(int type, GamePacket *payload, int source) override;
  void Update(float deltaTime);

  int GetServerTick() const;

  // Set the current server tick (used by client to track snapshot time)
  void SetServerTick(int tick);
  bool IsServer() const;
  int GetLocalPeerID() const;

  bool IsDedicatedServer() const;
  bool IsHost() const;
  bool IsClient() const;

  void SendRPCPacket(PacketStream &rpcStream, RPCReceiver target, int ownerID);

  ComponentPtr Copy(EntityPtr entityPtr) override;

  void RegisterComponent(NetworkComponent *networkComponent);
  void UnregisterComponent(NetworkComponent *networkComponent);
  void ClearRegisteredComponents();
  const std::vector<NetworkComponent *> &GetNetworkComponents() const {
    return m_networkComponents;
  }

TKDeclareParam(MultiChoiceVariant, Role)
    TKDeclareParam(bool, UseDeltaCompression)
        TKDeclareParam(MultiChoiceVariant, Preset)
            TKDeclareParam(bool, EnableInterpolation)
                TKDeclareParam(bool, EnableExtrapolation)
                    TKDeclareParam(bool, EnableLagCompensation)
                        TKDeclareParam(float, BufferTime)
                            TKDeclareParam(::ToolKit::ScenePtr,
                                           PlayerPrefab) protected :

    void UpdateAsServer(float deltaTime);
  void UpdateAsClient(float deltaTime);

  void BroadcastSnapshot();
  void SendSnapshotToPeer(int peerID, int baseTick);
  void UpdateMinimumState();

  void ParameterConstructor() override;

  const char *GetIPV4();

protected:
  std::map<int, int> stateIDs;

  MultiChoiceVariant m_role;
  // Internal helper to instantiate a network object from cache or prefab
  NetworkComponent *InstantiateNetworkObject(const std::string &typeOrPath,
                                             EntityPtr &outEntity);
  bool m_useDeltaCompression;
  MultiChoiceVariant m_preset;
  bool m_enableInterpolation;
  bool m_enableExtrapolation;
  bool m_enableLagCompensation;
  float m_bufferTime;

  int m_packetsToSnapshot;
  float m_timeToNextPacket;
  int m_nextNetworkID = 1;

  GameServerPtr m_server;
  GameClientPtr m_client;

  std::map<int, int> m_peerLastAckedTick;
  std::vector<NetworkComponent *> m_networkComponents;

  ScenePtr m_playerPrefab;

  PacketStream m_sendStream;
  PacketStream m_receiveStream;

  int m_currentServerTick = 0;
};
} // namespace ToolKit::ToolKitNetworking
