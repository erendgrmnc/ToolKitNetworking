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
		bool SendGlobalPacket(GamePacket& packet, bool reliable = false) const;
		bool SendGlobalPacket(int messageID) const;
		
		bool SendPacketToPeer(int peerID, GamePacket& packet, bool reliable = false) const;

		bool GetPeer(int peerIndex, int& peerId) const;
		int GetConnectedPeerCount() const { return (int)m_connectedPeers.size(); }
		const std::vector<int>& GetConnectedPeers() const { return m_connectedPeers; }

		std::string GetIpAddress() const;

		virtual void UpdateServer();
		void SetMaxClients(int maxClients);

		int GetServerTick() const { return m_serverTick; }

	protected:

		int	port;
		int	clientMax;

		int m_serverTick = 0;

		std::vector<int> m_connectedPeers;

		std::string m_ipAddress;

	};
}
