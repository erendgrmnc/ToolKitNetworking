#pragma once
#include "NetworkState.h"
#include <vector>
#include <cstring>

namespace ToolKit::ToolKitNetworking {


	enum NetworkMessage {
		None,
		Snapshot,
		Shutdown,
		ClientConnected
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

	struct WorldSnapshotPacket : public GamePacket {
		int serverTick;
		int entityCount;

		WorldSnapshotPacket() {
			type = NetworkMessage::Snapshot;
			size = 0;
			serverTick = 0;
			entityCount = 0;
		}
	};

	class PacketStream {
	public:
		std::vector<char> buffer;
		int readOffset = 0;

		PacketStream() {
			buffer.reserve(1024);
		}

		void Write(const void* data, size_t size) {
			size_t currentSize = buffer.size();
			buffer.resize(currentSize + size);
			std::memcpy(buffer.data() + currentSize, data, size);
		}

		template<typename T>
		void Write(const T& value) {
			Write(&value, sizeof(T));
		}

		void WriteInt(int value) { Write(value); }
		void WriteShort(short value) { Write(value); }
		void WriteFloat(float value) { Write(value); }
		void WriteBool(bool value) { Write(value); }

		template<typename T>
		bool Read(T& value) {
			if (readOffset + sizeof(T) > buffer.size()) return false;
			std::memcpy(&value, buffer.data() + readOffset, sizeof(T));
			readOffset += sizeof(T);
			return true;
		}

		bool ReadInt(int& value) { return Read(value); }
		bool ReadShort(short& value) { return Read(value); }
		bool ReadFloat(float& value) { return Read(value); }
		bool ReadBool(bool& value) { return Read(value); }

		void Clear() {
			buffer.clear();
			readOffset = 0;
		}

		void* GetData() { return buffer.data(); }
		size_t GetSize() const { return buffer.size(); }

		void Skip(size_t size) {
			readOffset += size;
		}
	};
}
