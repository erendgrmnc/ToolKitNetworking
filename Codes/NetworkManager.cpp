#include "NetworkManager.h"
#include "GameServer.h"
#include "GameClient.h"
#include "NetworkPackets.h"
#include "NetworkComponent.h"
#include <algorithm>


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
	m_serverTick = 0;

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
	m_client = MakeNewPtr<GameClient>();


	if (GameClient* client = m_client.get())
	{
		bool isConnected = client->Connect(host, portNum);
		if (isConnected) {
			client->RegisterPacketHandler(NetworkMessage::Snapshot, this);
			client->RegisterPacketHandler(NetworkMessage::ClientConnected, this);
			client->RegisterPacketHandler(NetworkMessage::Shutdown, this);
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::StartAsServer(uint16_t port) {
	m_server = MakeNewPtr<GameServer>(port, 2);

	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::ClientConnected, this);

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
				targetComponent->Deserialize(m_receiveStream, true);
				std::string logMsg = "Client received packet for object: " + std::to_string(networkID);
				TK_LOG(logMsg.c_str());
			}
			else {
				m_receiveStream.Skip(packetSize);
			}
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::BroadcastSnapshot() {
	if (!m_server) return;

	m_sendStream.Clear();

	WorldSnapshotPacket header;
	header.type = NetworkMessage::Snapshot;
	header.size = 0;
	header.serverTick = m_serverTick;
	header.entityCount = (int)m_networkComponents.size();
	m_sendStream.Write(header);

	for (auto* networkComponent : m_networkComponents) {
		networkComponent->Serialize(m_sendStream, true);

		std::string logMsg = "Packet sent for object: " + std::to_string(networkComponent->GetNetworkID());
		TK_LOG(logMsg.c_str());
	}

	size_t totalSize = m_sendStream.GetSize();
	WorldSnapshotPacket* packetHeader = (WorldSnapshotPacket*)m_sendStream.GetData();
	packetHeader->size = (short)(totalSize - sizeof(GamePacket));

	m_server->SendGlobalPacket(m_sendStream.GetData(), totalSize, false);
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
	m_serverTick++;
	BroadcastSnapshot();

	m_server.get()->UpdateServer();

}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateAsClient(float deltaTime) {
	m_client.get()->UpdateClient();
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateMinimumState() {
}

void ToolKit::ToolKitNetworking::NetworkManager::ParameterConstructor() {
	Component::ParameterConstructor();

	IsStartingAsServer_Define(m_isStartingAsServer, NetworkManagerCategory.Name, NetworkManagerCategory.Priority, true, true);
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
