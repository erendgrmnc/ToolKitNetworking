#pragma once
#include <Component.h>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include "NetworkPackets.h"
#include "NetworkVariable.h"
#include "NetworkMacros.h"

namespace ToolKit
{
	class Entity;

	namespace ToolKitNetworking
	{
		class NetworkState;
		struct GamePacket;

		typedef std::shared_ptr<class NetworkComponent> NetworkComponentPtr;
		typedef std::vector<NetworkComponentPtr> NetworkComponentPtrArray;

		enum class RPCReceiver
		{
			Server,
			Owner,
			Others,
			All
		};

		struct RPCPacket : public GamePacket
		{
			int networkID;
			uint32_t functionHash;
			// Data follows in the stream
			
			RPCPacket()
			{
				type = NetworkMessage::RPC;
				size = 0;
				networkID = -1;
				functionHash = 0;
			}
		};

		typedef std::function<void(PacketStream&)> RPCFunction;

		static VariantCategory NetworkComponentCategory{ "Network Component", 90 };

		class TK_NET_API NetworkComponent : public ToolKit::Component
		{
		public:
			TKDeclareClass(NetworkComponent, Component)

			NetworkComponent();
			virtual ~NetworkComponent();

			// Lifecycle
			virtual void OnNetworkSpawn() {}
			virtual void OnNetworkDespawn() {}

			// Identity & Authority
			void SetNetworkID(int id);
			int GetNetworkID() const;
			
			void SetOwnerID(int peerID);
			int GetOwnerID() const { return m_ownerPeerID; }

			bool IsOwner() const;
			bool IsServer() const;
			bool IsLocalPlayer() const;

			// SerializationT
			virtual void Serialize(PacketStream& stream, int baseTick);
			virtual void Deserialize(PacketStream& stream, int baseTick);

			// Network Variables
			void RegisterNetworkVariable(NetworkVariableBase* var);

			// RPCs
			void RegisterRPC(const std::string& name, RPCFunction func);
			
			template<typename... Args>
			void SendRPC(const std::string& name, RPCReceiver target, Args... args);

			// Internal RPC handling
			void HandleRPC(uint32_t hash, PacketStream& stream);

			// ToolKit Overrides
			void ParameterConstructor() override;
			ComponentPtr Copy(EntityPtr entityPtr) override;

			// Transform helpers
			EntityPtr GetEntity() const { return m_entity.lock(); }
			void UpdateStateHistory(int minID);

			NetworkState& GetLatestNetworkState();
			void SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState);

			void SetSpawnClassName(const std::string& name) { m_spawnClassName = name; }
			const std::string& GetSpawnClassName() const { return m_spawnClassName; }

		protected:
			bool GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state);
			uint32_t CalculateHash(const std::string& name);
			void SendRPCPacketInternal(PacketStream& stream, RPCReceiver target);

		protected:
			std::string m_spawnClassName;
			int networkID = -1;
			int m_ownerPeerID = -1; // -1 for Server/No owner

			std::vector<NetworkVariableBase*> m_networkVariables;
			std::map<uint32_t, RPCFunction> m_rpcHandlers;

			ToolKitNetworking::NetworkState lastFullState;
			std::vector<ToolKitNetworking::NetworkState> stateHistory;
		};
	}
}

namespace ToolKit::ToolKitNetworking
{
	template<typename... Args>
	void NetworkComponent::SendRPC(const std::string& name, RPCReceiver target, Args... args)
	{
		PacketStream rpcStream;
		RPCPacket header;
		header.networkID = this->networkID;
		header.functionHash = CalculateHash(name);
		
		rpcStream.Write(header);
		
		// Pack arguments
		([&](const auto& arg) {
			rpcStream.Write(arg);
		}(args), ...);

		RPCPacket* packedHeader = (RPCPacket*)rpcStream.GetData();
		packedHeader->size = (short)(rpcStream.GetSize() - sizeof(GamePacket));

		SendRPCPacketInternal(rpcStream, target);
	}
}

