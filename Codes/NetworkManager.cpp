#include "NetworkManager.h"
#include "GameServer.h"
#include "GameClient.h"
#include "NetworkPackets.h"
#include "NetworkComponent.h"
#include "NetworkSpawnService.h"
#include <Entity.h>
#include <Node.h>
#include <ToolKit.h>
#include <Scene.h>
#include <Prefab.h>
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

	if (std::find(m_networkComponents.begin(), m_networkComponents.end(), networkComponent) != m_networkComponents.end())
	{
		return;
	}

	if (IsServer() && networkComponent->GetOwnerID() == -1)
	{
		networkComponent->SetOwnerID(0);
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

	for (auto* networkComponent : m_networkComponents)
	{
		if (networkComponent->GetOwnerID() == -1)
		{
			networkComponent->SetOwnerID(0);
		}
	}

	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::ClientConnected, this);
	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::SnapshotAck, this);
	m_server->RegisterPacketHandler(ToolKitNetworking::NetworkMessage::RPC, this);

	const std::string serverLogStr =
		"Started as server on port " + std::to_string(port);
	TK_LOG(serverLogStr.c_str());

	// Spawn Server Player/Observer if needed or handle logic elsewhere
}

void ToolKit::ToolKitNetworking::NetworkManager::Stop()
{
	if (m_server) {
		m_server->Shutdown(); // Retained original shutdown call
		m_server = nullptr;
	}
	if (m_client) {
		m_client->Disconnect(); // Retained original disconnect call
		m_client = nullptr;
	}
	m_networkComponents.clear();
}

ToolKit::ToolKitNetworking::NetworkSpawnService& ToolKit::ToolKitNetworking::NetworkManager::GetSpawnService()
{
	return NetworkSpawnService::GetInstance();
}

ToolKit::ToolKitNetworking::NetworkComponent* ToolKit::ToolKitNetworking::NetworkManager::InstantiateNetworkObject(const std::string& typeOrPath, EntityPtr& outEntity)
{
	// Try Factory first
	NetworkComponent* netComp = GetSpawnService().Spawn(typeOrPath);
	outEntity = nullptr;

	if (netComp)
	{
		outEntity = std::make_shared<Entity>();
		outEntity->AddComponent(ComponentPtr(netComp));
		if (GetSceneManager()->GetCurrentScene()) {
			GetSceneManager()->GetCurrentScene()->AddEntity(outEntity);
		}
		return netComp;
	}

	// Try Prefab
	PrefabPtr prefab = std::make_shared<Prefab>();
	prefab->SetPrefabPathVal(typeOrPath);
	prefab->Load();

	prefab->Init(GetSceneManager()->GetCurrentScene());
	prefab->Link();
	if (GetSceneManager()->GetCurrentScene()) {
		GetSceneManager()->GetCurrentScene()->AddEntity(prefab);
	}

	// Find NetworkComponent in the instanced entities
	for (EntityPtr child : prefab->GetInstancedEntities())
	{
		if (netComp = child.get()->GetComponent<ToolKit::ToolKitNetworking::NetworkComponent>().get())
		{
			break;
		}
	}

	if (netComp)
	{
		outEntity = prefab;
		return netComp;
	}

	// Failed
	TK_LOG(("NetworkManager: InstantiateNetworkObject failed for: " + typeOrPath).c_str());
	if (GetSceneManager()->GetCurrentScene()) {
		GetSceneManager()->GetCurrentScene()->RemoveEntity(prefab->GetIdVal());
	}
	return nullptr;
}

ToolKit::ToolKitNetworking::NetworkComponent* ToolKit::ToolKitNetworking::NetworkManager::SpawnNetworkObject(const std::string& prefabName, int ownerID, const Vec3& pos, const Quaternion& rot)
{
	EntityPtr newEntity = nullptr;
	NetworkComponent* netComp = InstantiateNetworkObject(prefabName, newEntity);

	if (!netComp || !newEntity)
	{
		TK_LOG(("NetworkManager: Failed to spawn - Class factory not found/failed or Prefab invalid for type: " + prefabName).c_str());
		return nullptr;
	}

	newEntity->m_node->SetTranslation(pos);
	newEntity->m_node->SetOrientation(rot);

	if (netComp->GetNetworkID() == -1)
	{
		netComp->SetNetworkID(m_nextNetworkID++);
	}
	netComp->SetOwnerID(ownerID);
	
	if (netComp->GetSpawnClassName().empty())
	{
		netComp->SetSpawnClassName(prefabName);
	}

	netComp->OnNetworkSpawn();

	if (IsServer())
	{
		SpawnPacket packet;
		packet.networkID = netComp->GetNetworkID();
		packet.ownerID = ownerID;
		packet.px = pos.x; packet.py = pos.y; packet.pz = pos.z;
		packet.rx = rot.x; packet.ry = rot.y; packet.rz = rot.z; packet.rw = rot.w;
		strncpy(packet.className, prefabName.c_str(), 127);

		m_server->SendGlobalPacket(packet, true);
	}

	return netComp;
}

void ToolKit::ToolKitNetworking::NetworkManager::DespawnNetworkObject(NetworkComponent* component)
{
	if (!component) return;

	int netID = component->GetNetworkID();

	if (IsServer())
	{
		DespawnPacket packet;
		packet.networkID = netID;
		m_server->SendGlobalPacket(packet, true);
	}

	if (auto entity = component->GetEntity())
	{
		if (GetSceneManager()->GetCurrentScene()) {
			GetSceneManager()->GetCurrentScene()->RemoveEntity(entity->GetIdVal());
		}
	}
}

void ToolKit::ToolKitNetworking::NetworkManager::SendClientUpdate(NetworkComponent* component)
{
	if (!component || !m_client) return;
	
	if (auto entity = component->GetEntity())
	{
		Vec3 pos = entity->m_node->GetTranslation();
		Quaternion rot = entity->m_node->GetOrientation();

		ClientUpdatePacket packet;
		packet.networkID = component->GetNetworkID();
		packet.px = pos.x; packet.py = pos.y; packet.pz = pos.z;
		packet.rx = rot.x; packet.ry = rot.y; packet.rz = rot.z; packet.rw = rot.w;

		m_client->SendPacket(packet, false); // Unreliable for frequent position updates
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
// In ReceivePacket...
	else if (type == NetworkMessage::ClientConnected) {
		if (IsServer())
		{
			// Send all existing objects to the new client
			for (auto* nc : m_networkComponents)
			{
				SpawnPacket msg;
				msg.networkID = nc->GetNetworkID();
				msg.ownerID = nc->GetOwnerID();
				
				if (nc->GetSpawnClassName().empty())
				{
					TK_LOG("Warning: Replicating object with no SpanwClassName!");
					continue;
				}
				
				strncpy(msg.className, nc->GetSpawnClassName().c_str(), 127);
				
				if (auto ent = nc->GetEntity()) {
					Vec3 p = ent->m_node->GetTranslation();
					Quaternion r = ent->m_node->GetOrientation();
					msg.px = p.x; msg.py = p.y; msg.pz = p.z;
					msg.rx = r.x; msg.ry = r.y; msg.rz = r.z; msg.rw = r.w;
				}

				m_server->SendPacketToPeer(source, msg, true);
			}

			if (m_playerPrefab)
			{
				SpawnNetworkObject(m_playerPrefab->GetFile(), source, Vec3(0, 5, 0), Quaternion());
			}
			else
			{
				TK_LOG("NetworkManager: No PlayerPrefab configured! New client will not have a player object.");
			}
		}
	}
	else if (type == NetworkMessage::Spawn) {
		SpawnPacket* p = (SpawnPacket*)payload;
		
		bool exists = false;
		for (auto* nc : m_networkComponents) {
			if (nc->GetNetworkID() == p->networkID) { exists = true; break; }
		}

		if (!exists)
		{
			std::string className = p->className;
			EntityPtr newEntity = nullptr;
			NetworkComponent* netComp = InstantiateNetworkObject(className, newEntity);

			if (netComp && newEntity)
			{
				// Force ID from server
				netComp->SetNetworkID(p->networkID);
				netComp->SetOwnerID(p->ownerID);
				netComp->SetSpawnClassName(className);

				newEntity->m_node->SetTranslation(Vec3(p->px, p->py, p->pz));
				newEntity->m_node->SetOrientation(Quaternion(p->rw, p->rx, p->ry, p->rz));
				
				netComp->OnNetworkSpawn();
			}
			else
			{
				TK_LOG(("NetworkManager: Client failed to spawn object: " + className).c_str());
			}
		}
	}
	else if (type == NetworkMessage::Despawn) {
		DespawnPacket* p = (DespawnPacket*)payload;
		for (auto* nc : m_networkComponents) {
			if (nc->GetNetworkID() == p->networkID) {
				// Destroy
				if (auto ent = nc->GetEntity()) {
					if (GetSceneManager()->GetCurrentScene()) {
						GetSceneManager()->GetCurrentScene()->RemoveEntity(ent->GetIdVal());
					}
				}
				break;
			}
		}
	}
	else if (type == NetworkMessage::ClientUpdate) {
		if (IsServer())
		{
			ClientUpdatePacket* p = (ClientUpdatePacket*)payload;
			NetworkComponent* target = nullptr;
			for (auto* nc : m_networkComponents) {
				if (nc->GetNetworkID() == p->networkID) { target = nc; break; }
			}

			if (target && target->GetOwnerID() == source)
			{
				if (auto ent = target->GetEntity()) {
					ent->m_node->SetTranslation(Vec3(p->px, p->py, p->pz));
					ent->m_node->SetOrientation(Quaternion(p->rw, p->rx, p->ry, p->rz));
				}
			}
		}
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
	
	static float updateTimer = 0.0f;
	updateTimer += deltaTime;
	if (updateTimer >= 0.05f)
	{
		updateTimer = 0.0f;
		for (auto* nc : m_networkComponents)
		{
			if (nc->IsLocalPlayer())
			{
				SendClientUpdate(nc);
			}
		}
	}
}

int ToolKit::ToolKitNetworking::NetworkManager::GetServerTick() const {
	if (m_server) return m_server->GetServerTick();
	return 0;
}

bool ToolKit::ToolKitNetworking::NetworkManager::IsServer() const {
	return m_server != nullptr;
}

int ToolKit::ToolKitNetworking::NetworkManager::GetLocalPeerID() const {
	if (IsServer()) 
		return 0;
	if (m_client) 
		return m_client->GetPeerID();
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

	PlayerPrefab_Define(m_playerPrefab, NetworkManagerCategory.Name, NetworkManagerCategory.Priority, true, true);
}

const char* ToolKit::ToolKitNetworking::NetworkManager::GetIPV4()
{
	static char ipBuffer[64];
	ipBuffer[0] = '\0';

#ifdef _WIN32
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
