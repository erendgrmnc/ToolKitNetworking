#include "NetworkComponent.h"
#include "NetworkManager.h"

#include <Entity.h>
#include <Node.h>

#include "NetworkPackets.h"

namespace ToolKit::ToolKitNetworking
{

	TKDefineClass(NetworkComponent, Component);

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

		lastFullState.SetPosition(finalPos);
		lastFullState.SetOrientation(finalRot);
		lastFullState.SetNetworkStateID(NetworkManager::Instance->GetServerTick());
		stateHistory.push_back(lastFullState);
	}

	int NetworkComponent::GetNetworkID() const
	{
		return networkID;
	}

	void NetworkComponent::UpdateStateHistory(int minID)
	{
		for (auto i = stateHistory.begin(); i < stateHistory.end();)
		{
			if ((*i).GetNetworkStateID() < minID)
			{
				i = stateHistory.erase(i);
			}
			else
				++i;
		}
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

	ComponentPtr NetworkComponent::Copy(EntityPtr ntt)
	{
		NetworkComponentPtr nc = MakeNewPtr<NetworkComponent>();
		nc->m_localData = m_localData;
		nc->m_entity = ntt;
		return nc;
	}

	bool NetworkComponent::GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state)
	{
		for (auto i = stateHistory.begin(); i < stateHistory.end(); ++i)
		{
			if (i->GetNetworkStateID() == stateID)
			{
				state = (*i);
				return true;
			}
		}

		return false;
	}
}
