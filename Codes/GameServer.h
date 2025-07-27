#pragma once
#include "NetworkBase.h"

namespace ToolKit::ToolKitNetworking {
	class GameServer : public NetworkBase {
	public:
		GameServer(int onPort, int maxClients);
		~GameServer();

		bool Initialise();
		void Shutdown();

		virtual void AddPeer(int peerNumber);

		bool SendGlobalReliablePacket(GamePacket& packet) const;
		bool SendGlobalPacket(GamePacket& packet) const;
		bool SendGlobalPacket(int messageID) const;

		bool GetPeer(int peerNumber, int& peerId) const;

		std::string GetIpAddress() const;

		virtual void UpdateServer();
		void SetMaxClients(int maxClients);
	protected:

		int	port;
		int	clientMax;
		int	clientCount;
		int* peers;

		char ipAddress[16];

	};
}
