#include "NetworkManager.h"
#include "GameServer.h"
#include "GameClient.h"
#include "NetworkPackets.h"
#include "NetworkComponent.h"
#include <Entity.h>
#include <Node.h>
#include <algorithm>
#include <cmath>


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#endif


namespace ToolKit::ToolKitNetworking {

	TKDefineClass(NetworkManager, Component);
	NetworkManager* NetworkManager::Instance = nullptr;
}


ToolKit::ToolKitNetworking::NetworkManager::NetworkManager() {
	Instance = this;
	m_server = nullptr;
	m_useDeltaCompression = true;
	
	ToolKit::MultiChoiceVariant roleVar;
	{
		ToolKit::ParameterVariant v((int)NetworkRole::None);
		v.m_name = "None";
		roleVar.Choices.push_back(v);
	}
	{
		ToolKit::ParameterVariant v((int)NetworkRole::Client);
		v.m_name = "Client";
		roleVar.Choices.push_back(v);
	}
	{
		ToolKit::ParameterVariant v((int)NetworkRole::DedicatedServer);
		v.m_name = "DedicatedServer";
		roleVar.Choices.push_back(v);
	}
	{
		ToolKit::ParameterVariant v((int)NetworkRole::Host);
		v.m_name = "Host";
		roleVar.Choices.push_back(v);
	}
	roleVar.CurrentVal.Index = 0;
	m_role = roleVar;

	NetworkBase::Initialise();
}

ToolKit::ToolKitNetworking::NetworkManager::~NetworkManager() {
	Stop();
	if (Instance == this) {
		Instance = nullptr;
	}
	NetworkBase::Destroy();
}

void ToolKit::ToolKitNetworking::NetworkManager::RegisterComponent(NetworkComponent* networkComponent) {
	if (networkComponent->GetNetworkID() == -1) {
		networkComponent->SetNetworkID(m_nextNetworkID++);
	}
	m_networkComponents.push_back(networkComponent);

	std::string logMsg = "NetworkComponent Registered: ID " + std::to_string(networkComponent->GetNetworkID());
	TK_LOG(logMsg.c_str());
}

void ToolKit::ToolKitNetworking::NetworkManager::UnregisterComponent(NetworkComponent* networkComponent) {
	auto it = std::remove(m_networkComponents.begin(), m_networkComponents.end(), networkComponent);
	if (it != m_networkComponents.end()) {
		m_networkComponents.erase(it, m_networkComponents.end());
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::StartAsClient(const std::string& host, int portNum) {
	if (m_client) {
		TK_LOG("Client already running. Stopping previous instance.");
		m_client->Disconnect();
		m_client = nullptr;
	}

	m_client = MakeNewPtr<GameClient>();


	if (GameClient* client = m_client.get())
	{
		bool isConnected = client->Connect(host, portNum);
		if (isConnected) {
			client->RegisterPacketHandler(NetworkMessage::Snapshot, this);
			client->RegisterPacketHandler(NetworkMessage::ClientConnected, this);
			client->RegisterPacketHandler(NetworkMessage::Shutdown, this);
			client->RegisterPacketHandler(NetworkMessage::RPC, this);
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::StartAsServer(uint16_t port) {
	if (m_server) {
		TK_LOG("Server already running. Stopping previous instance.");
		m_server->Shutdown();
		m_server = nullptr;
	}

	m_server = MakeNewPtr<GameServer>(port, 2);

	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::ClientConnected, this);
	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::SnapshotAck, this);
	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::RPC, this);

	const std::string serverLogStr =
		"Started as server on port " + std::to_string(port);
	TK_LOG(serverLogStr.c_str());
}

void ToolKit::ToolKitNetworking::NetworkManager::Stop() {
	if (m_server) {
		m_server->Shutdown();
		m_server = nullptr;
	}
	if (m_client) {
		m_client->Disconnect();
		m_client = nullptr;
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::ReceivePacket(int type, GamePacket* payload, int source) {
	if (type == NetworkMessage::Snapshot) {
		m_receiveStream.Clear();

		int totalSize = payload->GetTotalSize();
		m_receiveStream.Write((void*)payload, totalSize);

		m_receiveStream.readOffset = sizeof(WorldSnapshotPacket);
		WorldSnapshotPacket* packet = (WorldSnapshotPacket*)payload;

		int entityCount = packet->entityCount;
		int baseTick = packet->baseTick;

		for (int i = 0; i < entityCount; i++) {
			int networkID = -1;
			if (!m_receiveStream.Read(networkID)) break;

			int packetSize = 0;
			if (!m_receiveStream.Read(packetSize)) break;

			NetworkComponent* targetComponent = nullptr;
			for (auto* networkComponent : m_networkComponents) {
				if (networkComponent->GetNetworkID() == networkID) {
					targetComponent = networkComponent;
					break;
				}
			}

			if (targetComponent) {
				targetComponent->Deserialize(m_receiveStream, baseTick);
			}
			else {
				m_receiveStream.Skip(packetSize);
			}
		}

		// Send Ack
		if (m_client) {
			SnapshotAckPacket ack;
			ack.ackTick = packet->serverTick;
			m_client->SendPacket(ack);
		}
	}
	else if (type == NetworkMessage::SnapshotAck) {
		SnapshotAckPacket* ack = (SnapshotAckPacket*)payload;
		m_peerLastAckedTick[source] = ack->ackTick;
	}
	else if (type == NetworkMessage::RPC) {
		RPCPacket* packet = (RPCPacket*)payload;
		
		m_receiveStream.Clear();
		m_receiveStream.Write((void*)payload, payload->GetTotalSize());
		m_receiveStream.readOffset = sizeof(RPCPacket);

		NetworkComponent* targetComponent = nullptr;
		for (auto* nc : m_networkComponents) {
			if (nc->GetNetworkID() == packet->networkID) {
				targetComponent = nc;
				break;
			}
		}

		if (targetComponent) {
			targetComponent->HandleRPC(packet->functionHash, m_receiveStream);
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::BroadcastSnapshot() {
	if (!m_server) return;

	int currentTick = m_server->GetServerTick();

	if (!m_useDeltaCompression) {
		m_sendStream.Clear();

		WorldSnapshotPacket header;
		header.type = NetworkMessage::Snapshot;
		header.size = 0;
		header.serverTick = currentTick;
		header.baseTick = -1;
		header.entityCount = (int)m_networkComponents.size();
		m_sendStream.Write(header);

		for (auto* networkComponent : m_networkComponents) {
			networkComponent->Serialize(m_sendStream, -1);
		}

		size_t totalSize = m_sendStream.GetSize();
		WorldSnapshotPacket* packetHeader = (WorldSnapshotPacket*)m_sendStream.GetData();
		packetHeader->size = (short)(totalSize - sizeof(GamePacket));

		m_server->SendGlobalPacket(*reinterpret_cast<GamePacket*>(m_sendStream.GetData()), false);
	}
	else {
		for (int peerID : m_server->GetConnectedPeers()) {
			int baseTick = -1;
			if (m_peerLastAckedTick.count(peerID)) {
				baseTick = m_peerLastAckedTick[peerID];
			}
			SendSnapshotToPeer(peerID, baseTick);
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::SendSnapshotToPeer(int peerID, int baseTick) {
	m_sendStream.Clear();

	WorldSnapshotPacket header;
	header.type = NetworkMessage::Snapshot;
	header.size = 0;
	header.serverTick = m_server->GetServerTick();
	header.baseTick = baseTick;
	header.entityCount = (int)m_networkComponents.size();
	m_sendStream.Write(header);

	for (auto* networkComponent : m_networkComponents) {
		networkComponent->Serialize(m_sendStream, baseTick);
	}

	size_t totalSize = m_sendStream.GetSize();
	WorldSnapshotPacket* packetHeader = (WorldSnapshotPacket*)m_sendStream.GetData();
	packetHeader->size = (short)(totalSize - sizeof(GamePacket));

	m_server->SendPacketToPeer(peerID, *reinterpret_cast<GamePacket*>(m_sendStream.GetData()), false); 
}

void ToolKit::ToolKitNetworking::NetworkManager::Update(float deltaTime)
{
	if (m_server.get())
	{
		UpdateAsServer(deltaTime);
	}

	if (m_client.get()) {
		UpdateAsClient(deltaTime);
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateAsServer(float deltaTime) {
	
	if (m_server)
	{
		m_server->UpdateServer(); 
	}

	BroadcastSnapshot();
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateAsClient(float deltaTime) {
	m_client.get()->UpdateClient();
}

int ToolKit::ToolKitNetworking::NetworkManager::GetServerTick() const {
	if (m_server) return m_server->GetServerTick();
	return 0;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsServer() const {
	return m_server != nullptr;
}

int ToolKit::ToolKitNetworking::NetworkManager::GetLocalPeerID() const {
	if (IsServer()) return 0;
	if (m_client) return m_client->GetPeerID();
	return -1;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsDedicatedServer() const {
	return m_role.GetEnum<NetworkRole>() == NetworkRole::DedicatedServer;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsHost() const {
	return m_role.GetEnum<NetworkRole>() == NetworkRole::Host;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsClient() const {
	return m_role.GetEnum<NetworkRole>() == NetworkRole::Client;
}

void ToolKit::ToolKitNetworking::NetworkManager::SendRPCPacket(PacketStream& rpcStream, RPCReceiver target, int ownerID) {
	GamePacket* packet = reinterpret_cast<GamePacket*>(rpcStream.GetData());
	if (IsServer()) {
		if (target == RPCReceiver::Server) {
			ReceivePacket(packet->type, packet, -1);
		}
		else if (target == RPCReceiver::All) {
			m_server->SendGlobalPacket(*packet, true);
		}
		else if (target == RPCReceiver::Owner) {
			if (GetLocalPeerID() == ownerID) {
				ReceivePacket(packet->type, packet, -1);
			}
			else if (ownerID != -1) {
				m_server->SendPacketToPeer(ownerID, *packet, true);
			}
		}
		else if (target == RPCReceiver::Others) {
			m_server->SendGlobalPacket(*packet, true);
		}
	}
	else if (m_client) {
		m_client->SendPacket(*packet, true);
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateMinimumState() {
}

void ToolKit::ToolKitNetworking::NetworkManager::ParameterConstructor() {
	Component::ParameterConstructor();

	Role_Define(m_role, NetworkManagerCategory.Name, NetworkManagerCategory.Priority, true, true);
	UseDeltaCompression_Define(m_useDeltaCompression, NetworkManagerCategory.Name, NetworkManagerCategory.Priority, true, true);
}

const char* ToolKit::ToolKitNetworking::NetworkManager::GetIPV4()
{
	static char ipBuffer[64];
	ipBuffer[0] = '\0';

#ifdef _WIN32
	// Initialize Winsock once per process ideally
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != SOCKET_ERROR)
	{
		addrinfo hints = {}, * info = nullptr;
		hints.ai_family = AF_INET; // IPv4
		hints.ai_socktype = SOCK_STREAM;

		if (getaddrinfo(hostname, nullptr, &hints, &info) == 0)
		{
			for (addrinfo* p = info; p != nullptr; p = p->ai_next)
			{
				sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(p->ai_addr);
				const char* addrStr = inet_ntoa(addr->sin_addr);

				// Skip loopback
				if (strcmp(addrStr, "127.0.0.1") != 0)
				{
					strncpy(ipBuffer, addrStr, sizeof(ipBuffer) - 1);
					break;
				}
			}

			freeaddrinfo(info);
		}
	}

	WSACleanup();

#else
	struct ifaddrs* ifaddr = nullptr;
	if (getifaddrs(&ifaddr) == 0)
	{
		for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
				continue;

			sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
			const char* addrStr = inet_ntop(AF_INET, &addr->sin_addr,
				ipBuffer, sizeof(ipBuffer));

			if (!addrStr)
				continue;

			// Skip loopback
			if (strcmp(ipBuffer, "127.0.0.1") != 0)
				break;
		}

		freeifaddrs(ifaddr);
	}
#endif

	// Fallback if no IP found
	if (ipBuffer[0] == '\0')
		strcpy(ipBuffer, "127.0.0.1");

	return ipBuffer;
}

ToolKit::ComponentPtr ToolKit::ToolKitNetworking::NetworkManager::Copy(EntityPtr entityPtr) {
	NetworkManagerPtr nc = MakeNewPtr<NetworkManager>();
	nc->m_localData = m_localData;
	nc->m_entity = entityPtr;
	return nc;
}
