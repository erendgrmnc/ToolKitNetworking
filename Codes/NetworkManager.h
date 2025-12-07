#pragma once
#include "NetworkBase.h"

namespace ToolKit::ToolKitNetworking {
	class GameServer;
	class GameClient;
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

		void StartAsClient(const std::string& host, int portNum);
		void StartAsServer(uint16_t port);

		void ReceivePacket(int type, GamePacket* payload, int source) override;
		void Update(float dt);

		ComponentPtr Copy(EntityPtr ntt) override;

		TKDeclareParam(bool, IsStartingAsServer)
	protected:

		void UpdateAsServer(float dt);
		void UpdateAsClient(float dt);

		void BroadcastSnapshot(bool isDeltaFrame);
		void UpdateMinimumState();

		void ParameterConstructor() override;


/**
 * @brief Retrieves the machine's primary IPv4 address (LAN/local only).
 *
 * @note TODO(erendegrmnc: This function does NOT return a public/WAN IP address.
 *       It only returns an IP that other devices on the same local network can use to connect.
 *       For WAN/public IP, a separate mechanism (e.g., master server or external API) is needed.
 *
 * @return const char* C-string containing the IPv4 address.
 */
		const char* GetIPV4();


	protected:
		std::map<int, int> stateIDs;

		bool m_isStartingAsServer;

		int m_packetsToSnapshot;
		float m_timeToNextPacket;

		GameServerPtr m_server;
		GameClientPtr m_client;

	};
}
