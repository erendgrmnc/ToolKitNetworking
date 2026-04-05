#pragma once
#include "ITransportHost.h"
#include "NetworkBase.h"
#include <vector>
#include <string>

namespace ToolKit::ToolKitNetworking {
	class GameServer : public NetworkBase, public ITransportHost {
	public:
		GameServer(const std::string& bindAddress, int onPort, int maxClients);
		~GameServer();

		bool Initialise();
		bool IsInitialised() const override { return m_netHandle != nullptr; }
		void Shutdown() override;

		virtual void AddPeer(int peerNumber);
		virtual void RemovePeer(int peerNumber);

		bool SendGlobalReliablePacket(GamePacket& packet) const override;
		bool SendGlobalPacket(GamePacket& packet, bool reliable = false) const override;
		bool SendGlobalPacket(int messageID) const override;
		
		bool SendPacketToPeer(TransportPeerId peerID, GamePacket& packet, bool reliable = false) const override;

		bool GetPeer(int peerIndex, int& peerId) const;
		int GetConnectedPeerCount() const override { return (int)m_connectedPeers.size(); }
		const std::vector<TransportPeerId>& GetConnectedPeers() const override { return m_connectedPeers; }

		std::string GetIpAddress() const override;

		virtual void UpdateServer() override;
		void SetMaxClients(int maxClients);
		void RegisterPacketHandler(int msgID, PacketReceiver* receiver) override { NetworkBase::RegisterPacketHandler(msgID, receiver); }
		void ClearPacketHandlers() override { NetworkBase::ClearPacketHandlers(); }

		int GetServerTick() const override { return m_serverTick; }

	protected:

		std::string m_bindAddress;
		int	port;
		int	clientMax;

		int m_serverTick = 0;

		std::vector<TransportPeerId> m_connectedPeers;

		std::string m_ipAddress;

	};
}
