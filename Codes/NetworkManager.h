#pragma once
#include "NetworkBase.h"
#include "ITransportHost.h"
#include "ITransportPeer.h"
#include "INetworkSessionRuntime.h"
#include "NetworkComponent.h"
#include "NetworkMacros.h"
#include "NetworkPackets.h"
#include "ReplicationManager.h"
#include "NetworkRole.h"
#include "NetworkSessionTypes.h"
#include "NetworkSpawnService.h"
#include <Component.h>
#include <functional>
#include <memory>
#include <vector>

namespace ToolKit::ToolKitNetworking {
class GameServer;
class GameClient;
class ITransportHost;
class ITransportPeer;
class NetworkComponent;
class NetworkSessionManager;
class NetworkSpawnService; // Forward declaration
class ReplicationManager;
enum class RPCReceiver;

enum class MovementPreset { Competitive, Smooth, Vehicle, Custom };

struct NetworkSettings {
  bool enableInterpolation = true;
  bool enableExtrapolation = false;
  bool enableLagCompensation = false;
  float bufferTime = 0.1f; // 100ms
};

typedef std::shared_ptr<class NetworkManager> NetworkManagerPtr;
typedef std::vector<NetworkManagerPtr> NetworkManagerPtrArray;

typedef std::shared_ptr<class ITransportPeer> TransportPeerPtr;
typedef std::shared_ptr<class ITransportHost> TransportHostPtr;

static VariantCategory NetworkManagerCategory{"NetworkManager", 100};

class TK_NET_API NetworkManager : public Component,
                                  public PacketReceiver,
                                  public INetworkSessionRuntime {
public:
  TKDeclareClass(NetworkManager, Component) NetworkManager();
  virtual ~NetworkManager();

  static NetworkManager *Instance;

  bool StartConfiguredSession();
  bool StartAsClient(const std::string &host, int portNum);
  bool StartAsServer(uint16_t port);
  void Stop();
  ConnectionStatus GetConnectionStatus() const;
  HostingMode GetHostingMode() const;
  const SessionDescriptor &GetActiveSession() const;
  const SessionHostRequest &GetLastHostRequest() const;
  const SessionJoinRequest &GetLastJoinRequest() const;

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
  SessionBootstrapConfig GetSessionBootstrapConfig() const override;
  HostingMode GetConfiguredHostingMode() const override;
  SessionDirectoryBrokerRuntimeConfig
  GetSessionDirectoryBrokerRuntimeConfig() const override;
  SessionDirectoryServiceBuildResult
  BuildSessionDirectoryService(
      const SessionDirectoryBrokerRuntimeConfig &config) const override;
  bool StartServerTransport(uint16_t port) override;
  bool StartClientTransport(const String &host, uint16_t port) override;
  void StopSessionTransports() override;
  bool HasServerTransport() const override;
  bool HasClientTransport() const override;
  bool IsClientTransportConnected() const override;
  bool BeginSessionHandshake(const SessionJoinRequest &request) override;
  bool IsSessionAuthenticated() const override;
  bool HasSessionAuthFailed() const override;
  DisconnectReason GetSessionAuthFailureReason() const override;
  String GetSessionAuthFailureDetail() const override;
  void SetReplicationClockNowProviderForTests(
      std::function<uint64_t()> clockNowProvider);

  void SendRPCPacket(PacketStream &rpcStream, RPCReceiver target, int ownerID);

  ComponentPtr Copy(EntityPtr entityPtr) override;

  void RegisterComponent(NetworkComponent *networkComponent);
  void UnregisterComponent(NetworkComponent *networkComponent);
  void ClearRegisteredComponents();
  const std::vector<NetworkComponent *> &GetNetworkComponents() const;

  TKDeclareParam(MultiChoiceVariant, Role)
  TKDeclareParam(bool, UseDeltaCompression)
  TKDeclareParam(MultiChoiceVariant, SessionJoinMethod)
  TKDeclareParam(String, ConnectHost)
  TKDeclareParam(uint, ConnectPort)
  TKDeclareParam(uint, ListenPort)
  TKDeclareParam(String, BindAddress)
  TKDeclareParam(String, AdvertisedAddress)
  TKDeclareParam(uint, MaxClients)
  TKDeclareParam(String, SessionId)
  TKDeclareParam(String, JoinCredential)
  TKDeclareParam(bool, RequireJoinCredential)
  TKDeclareParam(String, BuildCompatibilityId)
  TKDeclareParam(String, SessionDirectoryBrokerUrl)
  TKDeclareParam(String, SessionDirectoryBrokerAuthTokenEnvVar)
  TKDeclareParam(uint, SessionDirectoryBrokerTimeoutMs)
  TKDeclareParam(bool, AllowInsecureSessionDirectoryBrokerForLocalDev)
  TKDeclareParam(MultiChoiceVariant, Preset)
  TKDeclareParam(bool, EnableInterpolation)
  TKDeclareParam(bool, EnableExtrapolation)
  TKDeclareParam(bool, EnableLagCompensation)
  TKDeclareParam(float, BufferTime)
  TKDeclareParam(::ToolKit::ScenePtr, PlayerPrefab)

protected:
  void UpdateMinimumState();

  void ParameterConstructor() override;

  const char *GetIPV4();

  void ShutdownTransports();

protected:
  MultiChoiceVariant m_role;
  bool m_useDeltaCompression;
  MultiChoiceVariant m_sessionJoinMethod;
  String m_connectHost;
  uint m_connectPort;
  uint m_listenPort;
  String m_bindAddress;
  String m_advertisedAddress;
  uint m_maxClients;
  String m_sessionId;
  String m_joinCredential;
  bool m_requireJoinCredential;
  String m_buildCompatibilityId;
  String m_sessionDirectoryBrokerUrl;
  String m_sessionDirectoryBrokerAuthTokenEnvVar;
  uint m_sessionDirectoryBrokerTimeoutMs;
  bool m_allowInsecureSessionDirectoryBrokerForLocalDev;
  MultiChoiceVariant m_preset;
  bool m_enableInterpolation;
  bool m_enableExtrapolation;
  bool m_enableLagCompensation;
  float m_bufferTime;

  TransportHostPtr m_server;
  TransportPeerPtr m_client;
  std::unique_ptr<NetworkSessionManager> m_sessionManager;
  std::unique_ptr<ReplicationManager> m_replicationManager;

  ScenePtr m_playerPrefab;

  friend class ReplicationManager;
};
} // namespace ToolKit::ToolKitNetworking
