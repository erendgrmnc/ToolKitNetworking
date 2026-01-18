#include "GameClient.h"
#include "NetworkPackets.h"
#include <iostream>

ToolKit::ToolKitNetworking::GameClient::GameClient()
{
	m_netHandle = enet_host_create(nullptr, 1, 1, 0, 0);
	m_timerSinceLastPacket = 0.f;
	m_PeerId = -1;
	m_isConnected = false;
	m_clientSideLastFullID = -1;
}

ToolKit::ToolKitNetworking::GameClient::~GameClient()
{
}

bool ToolKit::ToolKitNetworking::GameClient::Connect(const std::string& host, int portNum)
{
	ENetAddress address;
	address.port = portNum;

	if (enet_address_set_host(&address, host.c_str()) != 0)
	{
		return false;
	}

	m_netPeer = enet_host_connect(m_netHandle, &address, 2, 0);

	return m_netPeer != nullptr;
}

bool ToolKit::ToolKitNetworking::GameClient::UpdateClient()
{
	if (m_netHandle == nullptr)
		return false;

	m_timerSinceLastPacket++;

	ENetEvent event;
	while (enet_host_service(m_netHandle, &event, 0) > 0) {
		if (event.type == ENET_EVENT_TYPE_CONNECT) {
			m_PeerId = m_netPeer->outgoingPeerID + 1;
			m_isConnected = true;

			for (const auto& callback : m_onClientConnectedToServer) {
				callback();
			}

			SendClientInitPacket();
		}
		else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
			GamePacket* packet = (GamePacket*)event.packet->data;
			if (!ProcessPacket(packet)) {
				TK_LOG("Client: Failed to process packet (No handler?)");
			}
			m_timerSinceLastPacket = 0.0f;
		}

		enet_packet_destroy(event.packet);
	}
	if (m_timerSinceLastPacket > 20.0f) {
		return false;
	}
	return true;
}

bool ToolKit::ToolKitNetworking::GameClient::GetIsConnected() const
{
	return false;
}

int ToolKit::ToolKitNetworking::GameClient::GetPeerID() const
{
	return m_PeerId;
}

const int ToolKit::ToolKitNetworking::GameClient::GetClientLastFullID() const
{
	return m_clientSideLastFullID;
}

void ToolKit::ToolKitNetworking::GameClient::SetClientLastFullID(const int clientLastFullID)
{
	m_clientSideLastFullID = clientLastFullID;
}

void ToolKit::ToolKitNetworking::GameClient::SendPacket(GamePacket& payload)
{
	int totalPacketSize = payload.GetTotalSize();
	ENetPacket* dataPacket = enet_packet_create(&payload, totalPacketSize, 0);
	enet_peer_send(m_netPeer, 0, dataPacket);
}

void ToolKit::ToolKitNetworking::GameClient::SendReliablePacket(GamePacket& payload) const
{
}

void ToolKit::ToolKitNetworking::GameClient::Disconnect()
{
	if (m_netPeer) {
		enet_peer_disconnect_now(m_netPeer, 0);
		m_netPeer = nullptr;
	}
	m_isConnected = false;
}

void ToolKit::ToolKitNetworking::GameClient::AddOnClientConnected(const std::function<void()>& callback)
{
}

std::string ToolKit::ToolKitNetworking::GameClient::GetIPAddress()
{
	return std::string();
}

void ToolKit::ToolKitNetworking::GameClient::SendClientInitPacket()
{
	
}
