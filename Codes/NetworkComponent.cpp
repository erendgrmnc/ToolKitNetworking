#include "NetworkComponent.h"
#include "NetworkManager.h"

#include <Entity.h>

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

	void NetworkComponent::Serialize(PacketStream& stream, bool fullState)
	{
		if (fullState) {
			stream.WriteInt(networkID);

			int sizeOffset = (int)stream.GetSize();
			int placeholderSize = 0;
			stream.WriteInt(placeholderSize);

			auto entity = m_entity.lock();
			if (entity && entity->m_node) {
				Vec3 position = entity->m_node->GetTranslation();
				stream.WriteFloat(position.x);
				stream.WriteFloat(position.y);
				stream.WriteFloat(position.z);

				Quaternion rotation = entity->m_node->GetOrientation();
				stream.WriteFloat(rotation.x);
				stream.WriteFloat(rotation.y);
				stream.WriteFloat(rotation.z);
				stream.WriteFloat(rotation.w);

				int currentSize = (int)stream.GetSize();
				int dataSize = currentSize - sizeOffset - sizeof(int);
				std::memcpy(stream.buffer.data() + sizeOffset, &dataSize, sizeof(int));

				NetworkState state;
				state.SetPosition(position);
				state.SetOrientation(rotation);
				lastFullState = state;
			}
			else {
				int currentSize = (int)stream.GetSize();
				int dataSize = currentSize - sizeOffset - sizeof(int);
				std::memcpy(stream.buffer.data() + sizeOffset, &dataSize, sizeof(int));
			}
		}
	}

	void NetworkComponent::Deserialize(PacketStream& stream, bool fullState)
	{
		if (fullState) {
			float positionX, positionY, positionZ;
			stream.ReadFloat(positionX); stream.ReadFloat(positionY); stream.ReadFloat(positionZ);

			float rotationX, rotationY, rotationZ, rotationW;
			stream.ReadFloat(rotationX); stream.ReadFloat(rotationY); stream.ReadFloat(rotationZ); stream.ReadFloat(rotationW);

			Vec3 position = Vec3(positionX, positionY, positionZ);
			Quaternion rotation = Quaternion(rotationX, rotationY, rotationZ, rotationW);

			auto entity = m_entity.lock();
			if (entity && entity->m_node) {
				entity->m_node->SetTranslation(position);
				entity->m_node->SetOrientation(rotation);
			}

			lastFullState.SetPosition(position);
			lastFullState.SetOrientation(rotation);
		}
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
