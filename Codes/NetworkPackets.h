#pragma once
#include "NetworkState.h"

namespace ToolKitNetworking {


	enum NetworkMessage {
		None,
		DeltaState,
		FullState
	};

	struct GamePacket {
		short size;
		short type;

		GamePacket() {
			type = NetworkMessage::None;
			size = 0;
		}

		GamePacket(short type) : GamePacket() {
			this->type = type;
		}

		int GetTotalSize() const {
			return sizeof(GamePacket) + size;
		}
	};

	struct FullPacket : public GamePacket {
		int fullPacketID = -1;
		int objectID = -1;

		ToolKitNetworking::NetworkState fullState;

		FullPacket() {
			type = ToolKitNetworking::NetworkMessage::FullState;
			size = sizeof(FullPacket) - sizeof(GamePacket);
		}
	};

	struct DeltaPacket : public GamePacket {
		int fullID = -1;
		int objectID = -1;
		int serverID = -1;

		char position[3] = {};
		char orientation[4] = {};

		DeltaPacket() {
			type = ToolKitNetworking::NetworkMessage::DeltaState;
			size = sizeof(DeltaPacket) - sizeof(GamePacket);
		}
	};
}
