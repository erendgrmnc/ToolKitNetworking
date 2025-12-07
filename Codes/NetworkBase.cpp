#include "NetworkBase.h"
#include <iostream>

#include "NetworkPackets.h"

namespace ToolKit::ToolKitNetworking {
	void NetworkBase::Initialise() {
		enet_initialize();
	}

	void NetworkBase::Destroy() {
		enet_deinitialize();
	}

	int NetworkBase::GetDefaultPort() {
		return 1234;
	}

	NetworkBase::NetworkBase() {
		m_netHandle = nullptr;
	}

	NetworkBase::~NetworkBase() {
		if (m_netHandle) {
			enet_host_destroy(m_netHandle);
		}
	}

	void NetworkBase::RegisterPacketHandler(int msgID, PacketReceiver* receiver) {
		packetHandlers.insert(std::make_pair(msgID, receiver));
	}

	void NetworkBase::ClearPacketHandlers() {
		packetHandlers.clear();
	}

	bool NetworkBase::ProcessPacket(GamePacket* packet, int peerID) const {
		PacketHandlerIterator firstHandler;
		PacketHandlerIterator lastHandler;

		if (GetPacketHandlers(packet->type, firstHandler, lastHandler)) {
			for (auto i = firstHandler; i != lastHandler; i++) {
				i->second->ReceivePacket(packet->type, packet, peerID);
			}
			return true;
		}

		std::cout << __FUNCTION__ << " no handler for packet type" << packet->type << std::endl;

		return false;
	}

	bool NetworkBase::GetPacketHandlers(int msgID, PacketHandlerIterator& first, PacketHandlerIterator& last) const {
		auto range = packetHandlers.equal_range(msgID);

		if (range.first == packetHandlers.end()) {
			return false; // no handlers for this message type!
		}

		first = range.first;
		last = range.second;
		return true;
	}

}

