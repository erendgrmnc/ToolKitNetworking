#include "GameClient.h"
#include "NetworkPackets.h"

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
	enet_host_destroy(m_netHandle);
}

bool ToolKit::ToolKitNetworking::GameClient::Connect(const std::string& host, int portNum)
{
	ENetAddress address;
	address.port = portNum;

	// ENet parses and converts the host string internally
	if (enet_address_set_host(&address, host.c_str()) != 0)
	{
		//TK_LOG("Failed to resolve host.");
		return false;
	}

	m_netPeer = enet_host_connect(m_netHandle, &address, 2, 0);

	return m_netPeer != nullptr;
}

bool ToolKit::ToolKitNetworking::GameClient::UpdateClient()
{
	// if there is no net handle we cannot handle packets
	if (m_netHandle == nullptr)
		return false;

	m_timerSinceLastPacket++;

	// handle incoming packets
	ENetEvent event;
	while (enet_host_service(m_netHandle, &event, 0) > 0) {
		if (event.type == ENET_EVENT_TYPE_CONNECT) {
			//erendgrmnc: I remember +1 is needed because when counting server as a player, outgoing peer Id is not increasing.
			m_PeerId = m_netPeer->outgoingPeerID + 1;
			m_isConnected = true;
			//TK_LOG("Connected to server!");

			for (const auto& callback : m_onClientConnectedToServer) {
				callback();
			}

			//TODO(eren.degirmenci): send player init packet.
			SendClientInitPacket();
		}
		else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
			//std::cout << "Client Packet recieved..." << std::endl;
			GamePacket* packet = (GamePacket*)event.packet->data;
			ProcessPacket(packet);
			m_timerSinceLastPacket = 0.0f;
		}
		// once packet data is handled we can destroy packet and go to next
		enet_packet_destroy(event.packet);
	}
	// return false if client is no longer receiving packets
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
