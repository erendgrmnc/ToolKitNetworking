#include "GameServer.h"

#include "NetworkPackets.h"

ToolKit::ToolKitNetworking::GameServer::GameServer(int onPort, int maxClients) {
	port = onPort;
	clientMax = maxClients;
	clientCount = 0;
	netHandle = nullptr;
	peers = new int[20];
	for (int i = 0; i < clientMax; ++i) {
		peers[i] = -1;
	}

	Initialise();
}

ToolKit::ToolKitNetworking::GameServer::~GameServer() {
	Shutdown();
}

bool ToolKit::ToolKitNetworking::GameServer::Initialise() {
	// create game server
	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = port;

	netHandle = enet_host_create(&address, clientMax, 1, 0, 0);

	if (!netHandle) {
		std::string functionName = __FUNCTION__;
		std::string logStr = functionName + "failed to create network handle!";

		TK_LOG(logStr.c_str());
		return false;
	}

	char ipString[16];
	enet_address_get_host_ip(&netHandle->address, ipString, sizeof(ipString));

	return true;
}

void ToolKit::ToolKitNetworking::GameServer::Shutdown() {
	SendGlobalPacket(NetworkMessage::Shutdown);
	enet_host_destroy(netHandle);
	netHandle = nullptr;
}

void ToolKit::ToolKitNetworking::GameServer::AddPeer(int peerNumber) {
	int emptyIndex = clientMax;
	for (int i = 0; i < clientMax; i++) {
		if (peers[i] == peerNumber) {
			return;
		}
		if (peers[i] == -1) {
			emptyIndex = min(i, emptyIndex);
		}
	}
	if (emptyIndex < clientMax) {
		peers[emptyIndex] = peerNumber;
		clientCount++;
	}
}

bool ToolKit::ToolKitNetworking::GameServer::SendGlobalReliablePacket(GamePacket& packet) const {
	ENetPacket* dataPacket = enet_packet_create(&packet, packet.GetTotalSize(), ENET_PACKET_FLAG_RELIABLE);
	enet_host_broadcast(netHandle, 0, dataPacket);
	return true;
}

bool ToolKit::ToolKitNetworking::GameServer::SendGlobalPacket(GamePacket& packet) const {
	ENetPacket* dataPacket = enet_packet_create(&packet, packet.GetTotalSize(), 0);
	enet_host_broadcast(netHandle, 0, dataPacket);
	return true;
}

bool ToolKit::ToolKitNetworking::GameServer::SendGlobalPacket(int messageID) const {
	GamePacket packet;
	packet.type = messageID;
	return SendGlobalPacket(packet);
}

bool ToolKit::ToolKitNetworking::GameServer::GetPeer(int peerNumber, int& peerId) const {
	if (peerNumber >= clientMax)
		return false;
	if (peers[peerNumber] == -1) {
		return false;
	}
	peerId = peers[peerNumber];
	return true;
}

std::string ToolKit::ToolKitNetworking::GameServer::GetIpAddress() const {
	return ipAddress;
}

void ToolKit::ToolKitNetworking::GameServer::UpdateServer() {
	if (!netHandle) { return; }

	ENetEvent event;
	while (enet_host_service(netHandle, &event, 0) > 0) {
		int type = event.type;
		ENetPeer* p = event.peer;
		int peer = p->incomingPeerID;

		if (type == ENetEventType::ENET_EVENT_TYPE_CONNECT) {
			TK_LOG("Server: New client has connected");
			AddPeer(peer + 1);
		}
		else if (type == ENetEventType::ENET_EVENT_TYPE_DISCONNECT) {
			TK_LOG("Server: Client has disconnected");
			for (int i = 0; i < 3; ++i) {
				if (peers[i] == peer + 1) {
					peers[i] = -1;
				}
			}

		}
		else if (type == ENetEventType::ENET_EVENT_TYPE_RECEIVE) {
			GamePacket* packet = reinterpret_cast<GamePacket*>(event.packet->data);
			ProcessPacket(packet, peer);
		}
		enet_packet_destroy(event.packet);
	}
}

void ToolKit::ToolKitNetworking::GameServer::SetMaxClients(int maxClients) {
	clientMax = maxClients;
}


