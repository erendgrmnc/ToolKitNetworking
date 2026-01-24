#pragma once
#include "NetworkState.h"
#include <vector>
#include <cstring>

namespace ToolKit::ToolKitNetworking {


	enum NetworkMessage {
		None,
		Snapshot,
		Shutdown,
		ClientConnected,
		SnapshotAck
	};

	enum class NetworkProperty : unsigned char {
		None = 0,
		Position = 1 << 0,
		Orientation = 1 << 1,
		Scale = 1 << 2,
		All = 0xFF
	};

	inline NetworkProperty operator|(NetworkProperty a, NetworkProperty b) {
		return static_cast<NetworkProperty>(static_cast<unsigned char>(a) | static_cast<unsigned char>(b));
	}

	inline bool HasProperty(unsigned char mask, NetworkProperty prop) {
		return (mask & static_cast<unsigned char>(prop)) != 0;
	}

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

	struct SnapshotAckPacket : public GamePacket {
		int ackTick;

		SnapshotAckPacket() {
			type = NetworkMessage::SnapshotAck;
			size = sizeof(int);
			ackTick = -1;
		}
	};

	struct WorldSnapshotPacket : public GamePacket {
		int serverTick;
		int baseTick; // -1 for full state
		int entityCount;

		WorldSnapshotPacket() {
			type = NetworkMessage::Snapshot;
			size = 0;
			serverTick = 0;
			baseTick = -1;
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
			if (readOffset + (int)sizeof(T) > (int)buffer.size()) return false;
			std::memcpy(&value, buffer.data() + readOffset, sizeof(T));
			readOffset += (int)sizeof(T);
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
			readOffset += (int)size;
		}
	};

	class PropertySerializer {
	public:
		PropertySerializer(PacketStream& stream) : m_stream(stream) {
			m_maskOffset = (int)m_stream.GetSize();
			m_stream.Write((unsigned char)0); // Placeholder for mask
		}

		~PropertySerializer() {
			std::memcpy(m_stream.buffer.data() + m_maskOffset, &m_mask, sizeof(unsigned char));
		}

		template<typename T>
		void Write(NetworkProperty prop, const T& value, bool changed) {
			if (changed) {
				m_mask |= static_cast<unsigned char>(prop);
				m_stream.Write(value);
			}
		}

	private:
		PacketStream& m_stream;
		int m_maskOffset;
		unsigned char m_mask = 0;
	};

	class PropertyDeserializer {
	public:
		PropertyDeserializer(PacketStream& stream) : m_stream(stream) {
			m_stream.Read(m_mask);
		}

		template<typename T>
		void Read(NetworkProperty prop, T& value, const T& defaultValue) {
			if (HasProperty(m_mask, prop)) {
				m_stream.Read(value);
			}
			else {
				value = defaultValue;
			}
		}

	private:
		PacketStream& m_stream;
		unsigned char m_mask = 0;
	};
}
