#pragma once
#include <Component.h>
#include "NetworkBase.h"
#include "NetworkPackets.h"
#include <vector>
#include <memory>
#include <map>

namespace ToolKit::ToolKitNetworking {
	class GameServer;
	class GameClient;
	class NetworkComponent;
}

namespace ToolKit::ToolKitNetworking {


	typedef std::shared_ptr<class NetworkManager> NetworkManagerPtr;
	typedef std::vector<NetworkManagerPtr> NetworkManagerPtrArray;

	typedef std::shared_ptr<class GameClient> GameClientPtr;
	typedef std::shared_ptr<class GameServer> GameServerPtr;


	static VariantCategory NetworkManagerCategory{ "NetworkManager", 100 };

	class TK_PLUGIN_API NetworkManager : public ToolKit::Component, public PacketReceiver {
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

		ComponentPtr Copy(EntityPtr entityPtr) override;

		void RegisterComponent(NetworkComponent* networkComponent);
		void UnregisterComponent(NetworkComponent* networkComponent);

		TKDeclareParam(bool, IsStartingAsServer)
		TKDeclareParam(bool, UseDeltaCompression)
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

		bool m_isStartingAsServer;
		bool m_useDeltaCompression;

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
