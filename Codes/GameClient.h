#pragma once
#include "NetworkBase.h"
#include <vector>
#include <functional>
#include <string>

namespace ToolKit::ToolKitNetworking {
	class GameClient : public NetworkBase {
	public:
		GameClient();
		~GameClient();

		bool Connect(const std::string& host, int portNum);
		bool UpdateClient();
		bool GetIsConnected() const;

		int GetPeerID() const;


		void SendPacket(GamePacket& payload, bool reliable = false);
		void SendReliablePacket(GamePacket& payload) const;
		void Disconnect();
		void AddOnClientConnected(const std::function<void()>& callback);

		std::string GetIPAddress();

	protected:
		bool m_isConnected;

		int m_PeerId;

		float m_timerSinceLastPacket;

		std::vector<std::function<void()>> m_onClientConnectedToServer;

		_ENetPeer* m_netPeer;


		void SendClientInitPacket();
	};

}
