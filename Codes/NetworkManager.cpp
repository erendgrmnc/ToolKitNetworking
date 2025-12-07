#include "NetworkManager.h"
#include "GameServer.h"
#include "GameClient.h"
#include "NetworkPackets.h"


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
}


ToolKit::ToolKitNetworking::NetworkManager::NetworkManager() {
	m_server = nullptr;

	NetworkBase::Initialise();
}

ToolKit::ToolKitNetworking::NetworkManager::~NetworkManager() {
}

void ToolKit::ToolKitNetworking::NetworkManager::StartAsClient(const std::string& host, int portNum) {
	m_client = MakeNewPtr<GameClient>();


	if (GameClient* client = m_client.get())
	{
		bool isConnected = client->Connect(host, portNum);
		if (isConnected) {
			client->RegisterPacketHandler(NetworkMessage::DeltaState, this);
			client->RegisterPacketHandler(NetworkMessage::FullState, this);
			client->RegisterPacketHandler(NetworkMessage::ClientConnected, this);
			client->RegisterPacketHandler(NetworkMessage::Shutdown, this);
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::StartAsServer(uint16_t port) {
	m_server = MakeNewPtr<GameServer>(NetworkBase::GetDefaultPort(), 2);

	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::ClientConnected, this);

	const std::string serverLogStr =
		"Started as server on port " + std::to_string(port);
	TK_LOG(serverLogStr.c_str());
}

void ToolKit::ToolKitNetworking::NetworkManager::ReceivePacket(int type, GamePacket* payload, int source) {
}

void ToolKit::ToolKitNetworking::NetworkManager::Update(float dt)
{
	if (m_server.get())
	{
		UpdateAsServer(dt);
	}

	if (m_client.get()) {
		UpdateAsClient(dt);
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateAsServer(float dt) {
	m_packetsToSnapshot--;
	if (m_packetsToSnapshot < 0) {
		// full packet
		BroadcastSnapshot(false);
		m_packetsToSnapshot = 5;
	}
	else {
		// use delta packets
		//BroadcastSnapshot(true);

		// dont use delta packets
		BroadcastSnapshot(false);
	}

	m_server.get()->UpdateServer();

}

void ToolKit::ToolKitNetworking::NetworkManager::UpdateAsClient(float dt) {
	m_client.get()->UpdateClient();
}

void ToolKit::ToolKitNetworking::NetworkManager::BroadcastSnapshot(bool isDeltaFrame) {
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

ToolKit::ComponentPtr ToolKit::ToolKitNetworking::NetworkManager::Copy(EntityPtr ntt) {
	NetworkManagerPtr nc = MakeNewPtr<NetworkManager>();
	nc->m_localData = m_localData;
	nc->m_entity = ntt;
	return nc;
}
