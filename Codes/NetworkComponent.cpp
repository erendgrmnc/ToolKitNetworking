#include "NetworkComponent.h"
#include "NetworkManager.h"
#include "NetworkRPCRegistry.h"
#include <Entity.h>
#include <Node.h>
#include <algorithm>

namespace ToolKit::ToolKitNetworking
{
	TKDefineClass(NetworkComponent, Component);

	NetworkComponent::NetworkComponent()
	{
		m_ownerPeerID = -1; // Default to Server-owned
	}

	NetworkComponent::~NetworkComponent()
	{
		if (NetworkManager::Instance)
		{
			NetworkManager::Instance->UnregisterComponent(this);
		}
	}

	void NetworkComponent::ParameterConstructor()
	{
		Component::ParameterConstructor();
		if (NetworkManager::Instance)
		{
			NetworkManager::Instance->RegisterComponent(this);
		}
	}

	void NetworkComponent::SetNetworkID(int id)
	{
		networkID = id;
	}

	int NetworkComponent::GetNetworkID() const
	{
		return networkID;
	}

	void NetworkComponent::SetOwnerID(int peerID)
	{
		m_ownerPeerID = peerID;
	}

	bool NetworkComponent::IsOwner() const
	{
		if (NetworkManager::Instance == nullptr) return false;
		return NetworkManager::Instance->GetLocalPeerID() == m_ownerPeerID;
	}

	bool NetworkComponent::IsServer() const
	{
		return NetworkManager::Instance && NetworkManager::Instance->IsServer();
	}

	bool NetworkComponent::IsLocalPlayer() const
	{
		return IsOwner();
	}

	void NetworkComponent::Serialize(PacketStream& stream, int baseTick)
	{
		stream.WriteInt(networkID);

		int sizeOffset = (int)stream.GetSize();
		int placeholderSize = 0;
		stream.WriteInt(placeholderSize);

		auto entity = m_entity.lock();
		if (entity && entity->m_node) {
			Vec3 currentPos = entity->m_node->GetTranslation();
			Quaternion currentRot = entity->m_node->GetOrientation();

			NetworkState baseState;
			bool hasBase = (baseTick != -1) && GetNetworkState(baseTick, baseState);

			PropertySerializer serializer(stream);
			
			bool posChanged = !hasBase || glm::distance(currentPos, baseState.GetPosition()) > 0.001f;
			bool rotChanged = !hasBase || std::abs(1.0f - std::abs(glm::dot(currentRot, baseState.GetOrientation()))) > 0.0001f;

			serializer.Write(NetworkProperty::Position, currentPos, posChanged);
			serializer.Write(NetworkProperty::Orientation, currentRot, rotChanged);

			// Network Variables
			bool anyVarDirty = false;
			for (auto* var : m_networkVariables)
			{
				if (var->IsDirty()) { anyVarDirty = true; break; }
			}

			if (anyVarDirty || !hasBase)
			{
				serializer.MarkAsChanged(NetworkProperty::NetworkVariables);
				stream.WriteInt((int)m_networkVariables.size());
				for (auto* var : m_networkVariables)
				{
					var->Serialize(stream);
					if (IsServer()) var->ResetDirty();
				}
			}

			NetworkState state;
			state.SetPosition(currentPos);
			state.SetOrientation(currentRot);
			state.SetNetworkStateID(NetworkManager::Instance->GetServerTick());
			SetLatestNetworkState(state);
		}

		int currentSize = (int)stream.GetSize();
		int dataSize = currentSize - sizeOffset - sizeof(int);
		std::memcpy(stream.buffer.data() + sizeOffset, &dataSize, sizeof(int));
	}

	void NetworkComponent::Deserialize(PacketStream& stream, int baseTick)
	{
		NetworkState baseState;
		bool hasBase = (baseTick != -1) && GetNetworkState(baseTick, baseState);

		PropertyDeserializer deserializer(stream);
		
		Vec3 finalPos;
		deserializer.Read(NetworkProperty::Position, finalPos, hasBase ? baseState.GetPosition() : Vec3(0, 0, 0));
		
		Quaternion finalRot;
		deserializer.Read(NetworkProperty::Orientation, finalRot, hasBase ? baseState.GetOrientation() : Quaternion());

		auto entity = m_entity.lock();
		if (entity && entity->m_node) {
			entity->m_node->SetLocalTransforms(finalPos, finalRot, entity->m_node->GetScale());
		}

		// Network Variables
		if (deserializer.Has(NetworkProperty::NetworkVariables))
		{
			int varCount = 0;
			stream.ReadInt(varCount);
			for (int i = 0; i < varCount && i < (int)m_networkVariables.size(); i++)
			{
				m_networkVariables[i]->Deserialize(stream);
			}
		}

		lastFullState.SetPosition(finalPos);
		lastFullState.SetOrientation(finalRot);
		lastFullState.SetNetworkStateID(NetworkManager::Instance->GetServerTick());
		stateHistory.push_back(lastFullState);
	}

	void NetworkComponent::RegisterNetworkVariable(NetworkVariableBase* var)
	{
		m_networkVariables.push_back(var);
	}

	void NetworkComponent::RegisterRPC(const std::string& name, RPCFunction func)
	{
		m_rpcHandlers[CalculateHash(name)] = func;
	}

	void NetworkComponent::HandleRPC(uint32_t hash, PacketStream& stream)
	{
		// 1. Check local manual handlers
		if (m_rpcHandlers.count(hash))
		{
			m_rpcHandlers[hash](stream);
			return;
		}

		// 2. Check global registry for macro-defined RPCs
		auto dispatcher = NetworkRPCRegistry::Instance().GetDispatcher(Class(), hash);
		if (dispatcher)
		{
			dispatcher(this, stream);
		}
	}

	void NetworkComponent::UpdateStateHistory(int minID)
	{
		auto it = std::remove_if(stateHistory.begin(), stateHistory.end(),
			[minID](const NetworkState& state) {
				return state.GetNetworkStateID() < minID;
			});
		stateHistory.erase(it, stateHistory.end());
	}

	NetworkState& NetworkComponent::GetLatestNetworkState()
	{
		return lastFullState;
	}

	void NetworkComponent::SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState)
	{
		lastFullState = lastState;
		stateHistory.push_back(lastFullState);
	}

	ComponentPtr NetworkComponent::Copy(EntityPtr entityPtr)
	{
		NetworkComponentPtr nc = MakeNewPtr<NetworkComponent>();
		nc->m_localData = m_localData;
		nc->m_entity = entityPtr;
		return nc;
	}

	bool NetworkComponent::GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state)
	{
		for (auto& entry : stateHistory)
		{
			if (entry.GetNetworkStateID() == stateID)
			{
				state = entry;
				return true;
			}
		}
		return false;
	}

	uint32_t NetworkComponent::CalculateHash(const std::string& name)
	{
		uint32_t hash = 5381;
		for (char c : name)
			hash = ((hash << 5) + hash) + c;
		return hash;
	}
}
