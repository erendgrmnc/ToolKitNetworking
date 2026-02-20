#pragma once
#include <Component.h>
#include "NetworkBase.h"
#include "NetworkComponent.h"
#include "NetworkPackets.h"
#include "NetworkMacros.h"
#include "NetworkSpawnService.h"
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <functional>
#include <functional>

namespace ToolKit::ToolKitNetworking {
	class GameServer;
	class GameClient;
	class NetworkComponent;
	class NetworkSpawnService; // Forward declaration
	enum class RPCReceiver;
	enum class NetworkRole {
		None,
		Client,
		DedicatedServer,
		Host
	};

	enum class MovementPreset {
		Competitive,
		Smooth,
		Vehicle,
		Custom
	};

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


	static VariantCategory NetworkManagerCategory{ "NetworkManager", 100 };

	class TK_NET_API NetworkManager : public Component, public PacketReceiver {
	public:

		TKDeclareClass(NetworkManager, Component)
			NetworkManager();
		virtual ~NetworkManager();

		static NetworkManager* Instance;

		void StartAsClient(const std::string& host, int portNum);
		void StartAsServer(uint16_t port);
		void Stop();


		static NetworkSpawnService& GetSpawnService();

		template<typename T>
		static void RegisterSpawnFactory()
		{
			GetSpawnService().Register<T>();
		}

		NetworkComponent* SpawnNetworkObject(const std::string& prefabName, int ownerID, const Vec3& pos, const Quaternion& rot);
		void DespawnNetworkObject(NetworkComponent* component);

		void SendClientUpdate(NetworkComponent* component);

		void ReceivePacket(int type, GamePacket* payload, int source) override;
		void Update(float deltaTime);

		int GetServerTick() const;
		bool IsServer() const;
		int GetLocalPeerID() const;

		bool IsDedicatedServer() const;
		bool IsHost() const;
		bool IsClient() const;

		void SendRPCPacket(PacketStream& rpcStream, RPCReceiver target, int ownerID);

		ComponentPtr Copy(EntityPtr entityPtr) override;

		void RegisterComponent(NetworkComponent* networkComponent);
		void UnregisterComponent(NetworkComponent* networkComponent);

		TKDeclareParam(MultiChoiceVariant, Role)
			TKDeclareParam(bool, UseDeltaCompression)
			TKDeclareParam(MultiChoiceVariant, Preset)
			TKDeclareParam(bool, EnableInterpolation)
			TKDeclareParam(bool, EnableExtrapolation)
			TKDeclareParam(bool, EnableLagCompensation)
			TKDeclareParam(float, BufferTime)
			TKDeclareParam(::ToolKit::PrefabPtr, PlayerPrefab)
	protected:

		void UpdateAsServer(float deltaTime);
		void UpdateAsClient(float deltaTime);

		void BroadcastSnapshot();
		void SendSnapshotToPeer(int peerID, int baseTick);
		void UpdateMinimumState();

		void ParameterConstructor() override;

		const char* GetIPV4();


	protected:
		std::map<int, int> stateIDs;

		MultiChoiceVariant m_role;
		// Internal helper to instantiate a network object from cache or prefab
		NetworkComponent* InstantiateNetworkObject(const std::string& typeOrPath, EntityPtr& outEntity);
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
		std::vector<NetworkComponent*> m_networkComponents;

		PrefabPtr m_playerPrefab;

		PacketStream m_sendStream;
		PacketStream m_receiveStream;

	};
} // namespace ToolKit::ToolKitNetworking
