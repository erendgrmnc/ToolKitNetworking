#pragma once
#include "NetworkBase.h"
#include <vector>
#include <string>

namespace ToolKit::ToolKitNetworking {
	class GameServer : public NetworkBase {
	public:
		GameServer(int onPort, int maxClients);
		~GameServer();

		bool Initialise();
		void Shutdown();

		virtual void AddPeer(int peerNumber);
		virtual void RemovePeer(int peerNumber);

		bool SendGlobalReliablePacket(GamePacket& packet) const;
		bool SendGlobalPacket(GamePacket& packet) const;
		bool SendGlobalPacket(int messageID) const;
		
		// New raw send
		bool SendGlobalPacket(const void* data, size_t size, bool reliable) const;

		bool GetPeer(int peerIndex, int& peerId) const;

		std::string GetIpAddress() const;

		virtual void UpdateServer();
		void SetMaxClients(int maxClients);
	protected:

		int	port;
		int	clientMax;

		std::vector<int> m_connectedPeers;

		std::string m_ipAddress;

	};
}
