#include "GameServer.h"
#include "NetworkPackets.h"
#include <algorithm>
#include <iostream>

using namespace ToolKit::ToolKitNetworking;

GameServer::GameServer(int onPort, int maxClients) {
	port = onPort;
	clientMax = maxClients;
	m_netHandle = nullptr;
	
	Initialise();
}

GameServer::~GameServer() {
	Shutdown();
}

bool GameServer::Initialise() {
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	m_netHandle = enet_host_create(&address, clientMax, 1, 0, 0);

	if (!m_netHandle) {
		std::string functionName = __FUNCTION__;
		std::string logStr = functionName + " failed to create network handle!";

		TK_LOG(logStr.c_str());
		return false;
	}

	char ipString[16];
	enet_address_get_host_ip(&m_netHandle->address, ipString, sizeof(ipString));
	m_ipAddress = std::string(ipString);

	return true;
}

void GameServer::Shutdown() {
	SendGlobalPacket(NetworkMessage::Shutdown);
	if (m_netHandle) {
		enet_host_destroy(m_netHandle);
		m_netHandle = nullptr;
	}
	m_connectedPeers.clear();
}

void GameServer::AddPeer(int peerNumber) {
	for (int p : m_connectedPeers) {
		if (p == peerNumber) {
			return;
		}
	}

	if (m_connectedPeers.size() >= clientMax) {
		TK_LOG("Server: Max clients reached, cannot add peer.");
		return;
	}
	
	m_connectedPeers.push_back(peerNumber);
}

void GameServer::RemovePeer(int peerNumber) {
	auto it = std::remove(m_connectedPeers.begin(), m_connectedPeers.end(), peerNumber);
	if (it != m_connectedPeers.end()) {
		m_connectedPeers.erase(it, m_connectedPeers.end());
	}
}

bool GameServer::SendGlobalReliablePacket(GamePacket& packet) const {
	if (!m_netHandle) return false;
	ENetPacket* dataPacket = enet_packet_create(&packet, packet.GetTotalSize(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(m_netHandle, 0, dataPacket);
	return true;
}

bool GameServer::SendGlobalPacket(GamePacket& packet) const {
	if (!m_netHandle) return false;
	ENetPacket* dataPacket = enet_packet_create(&packet, packet.GetTotalSize(), 0);
	enet_host_broadcast(m_netHandle, 0, dataPacket);
	return true;
}

bool GameServer::SendGlobalPacket(int messageID) const {
	GamePacket packet;
	packet.type = messageID;
	return SendGlobalPacket(packet);
}

bool GameServer::GetPeer(int peerIndex, int& peerId) const {
	if (peerIndex < 0 || peerIndex >= m_connectedPeers.size())
		return false;
	
	peerId = m_connectedPeers[peerIndex];
	return true;
}

std::string GameServer::GetIpAddress() const {
	return m_ipAddress;
}

void GameServer::UpdateServer() {
	if (!m_netHandle) { return; }

	ENetEvent event;
	while (enet_host_service(m_netHandle, &event, 0) > 0) {
		int type = event.type;
		ENetPeer* p = event.peer;
		int peer = p->incomingPeerID;

		if (type == ENetEventType::ENET_EVENT_TYPE_CONNECT) {
			TK_LOG("Server: New client has connected");
			AddPeer(peer + 1);
		}
		else if (type == ENetEventType::ENET_EVENT_TYPE_DISCONNECT) {
			TK_LOG("Server: Client has disconnected");
			RemovePeer(peer + 1);
		}
		else if (type == ENetEventType::ENET_EVENT_TYPE_RECEIVE) {
			GamePacket* packet = reinterpret_cast<GamePacket*>(event.packet->data);
			ProcessPacket(packet, peer);
		}
		enet_packet_destroy(event.packet);
	}
}

void GameServer::SetMaxClients(int maxClients) {
	clientMax = maxClients;
}
