#pragma once
#include "ITransportPeer.h"
#include "NetworkBase.h"
#include <vector>
#include <functional>
#include <string>

namespace ToolKit::ToolKitNetworking {
	class GameClient : public NetworkBase, public ITransportPeer {
	public:
		GameClient();
		~GameClient();

		bool Connect(const std::string& host, int portNum) override;
		bool UpdateClient() override;
		bool GetIsConnected() const override;

		TransportPeerId GetPeerID() const override;
		void SetPeerID(TransportPeerId peerID) override;


		void SendPacket(GamePacket& payload, bool reliable = false) override;
		void SendReliablePacket(GamePacket& payload) const;
		void Disconnect() override;
		void AddOnClientConnected(const std::function<void()>& callback);

		std::string GetIPAddress() override;
		void RegisterPacketHandler(int msgID, PacketReceiver* receiver) override { NetworkBase::RegisterPacketHandler(msgID, receiver); }
		void ClearPacketHandlers() override { NetworkBase::ClearPacketHandlers(); }

	protected:
		bool m_isConnected;

		int m_PeerId;

		float m_timerSinceLastPacket;

		std::vector<std::function<void()>> m_onClientConnectedToServer;

		_ENetPeer* m_netPeer;


		void SendClientInitPacket();
	};

}
