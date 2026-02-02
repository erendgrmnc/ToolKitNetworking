#pragma once
#include <Component.h>
#include "NetworkBase.h"
#include "NetworkPackets.h"
#include "NetworkMacros.h"
#include <vector>
#include <memory>
#include <map>

namespace ToolKit::ToolKitNetworking {
	class GameServer;
	class GameClient;
	class NetworkComponent;
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
	}

	namespace ToolKit::ToolKitNetworking {



	typedef std::shared_ptr<class NetworkManager> NetworkManagerPtr;
	typedef std::vector<NetworkManagerPtr> NetworkManagerPtrArray;

	typedef std::shared_ptr<class GameClient> GameClientPtr;
	typedef std::shared_ptr<class GameServer> GameServerPtr;


	static VariantCategory NetworkManagerCategory{ "NetworkManager", 100 };

	class TK_NET_API NetworkManager : public ToolKit::Component, public PacketReceiver {
	public:

		TKDeclareClass(NetworkManager, Component)
			NetworkManager();
		virtual ~NetworkManager();

		static NetworkManager* Instance;

		void StartAsClient(const std::string& host, int portNum);
		void StartAsServer(uint16_t port);
		void Stop();

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

		TKDeclareParam(ToolKit::MultiChoiceVariant, Role)
		TKDeclareParam(bool, UseDeltaCompression)
		TKDeclareParam(ToolKit::MultiChoiceVariant, Preset)
		TKDeclareParam(bool, EnableInterpolation)
		TKDeclareParam(bool, EnableExtrapolation)
		TKDeclareParam(bool, EnableLagCompensation)
		TKDeclareParam(float, BufferTime)
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

		ToolKit::MultiChoiceVariant m_role;
		bool m_useDeltaCompression;
		ToolKit::MultiChoiceVariant m_preset;
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
	
		PacketStream m_sendStream;
		PacketStream m_receiveStream;

	};
}
