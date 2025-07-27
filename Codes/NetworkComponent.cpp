#include "NetworkComponent.h"

#include <Entity.h>

#include "NetworkPackets.h"

namespace ToolKit::ToolKitNetworking {

	TKDefineClass(NetworkComponent, Component);

	void NetworkComponent::InitNetworkObject(ToolKit::Entity& entity, int id) {
		networkID = id;
		this->entity = &entity;
	}

	bool NetworkComponent::ReadPacket(GamePacket& packet) {
		if (packet.type == NetworkMessage::DeltaState)
			return ReadDeltaPacket((DeltaPacket&)packet);

		if (packet.type == NetworkMessage::FullState)
			return ReadFullPacket((FullPacket&)packet);
	}

	bool NetworkComponent::WritePacket(GamePacket** packet, bool deltaFrame, int stateID) {
		if (deltaFrame) {
			if (!WriteDeltaPacket(packet, stateID)) {
				return WriteFullPacket(packet);
			}
			return true;
		}
		return WriteFullPacket(packet);
	}

	int NetworkComponent::GetNetworkID() const {
		return networkID;
	}

	void NetworkComponent::UpdateStateHistory(int minID) {
		for (auto i = stateHistory.begin(); i < stateHistory.end();) {
			if ((*i).GetNetworkStateID() < minID) {
				//std::cout << "Removing State: " << i->stateID << "\n";
				i = stateHistory.erase(i);
			}
			else
				++i;
		}
	}

	NetworkState& NetworkComponent::GetLatestNetworkState() {
		return lastFullState;
	}

	void NetworkComponent::SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState) {
		lastFullState = lastState;
		stateHistory.push_back(lastFullState);
	}

	ComponentPtr NetworkComponent::Copy(EntityPtr ntt) {
		NetworkComponentPtr nc = MakeNewPtr<NetworkComponent>();
		nc->m_localData = m_localData;
		nc->m_entity = ntt;
		return nc;
	}

	bool NetworkComponent::GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state) {
		// get a state ID from state history if needed
		for (auto i = stateHistory.begin(); i < stateHistory.end(); ++i) {
			if (i->GetNetworkStateID() == stateID) {
				state = (*i);
				//std::cout << "Successfully found network state.State ID: " << stateID << "\n";
				return true;

			}
		}
		//std::cout << "Couldn't find state for ID: " << stateID << ", stateHistorySize: " << stateHistory.size() << "\n";
		return false;
	}

	bool NetworkComponent::ReadDeltaPacket(ToolKitNetworking::DeltaPacket& packet) {
		if (packet.fullID != lastFullState.GetNetworkStateID())
			return false;
		UpdateStateHistory(packet.fullID);

		Vec3 fullPos = lastFullState.GetPosition();

		Quaternion fullOrientation = lastFullState.GetOrientation();

		fullPos.x += packet.position[0];
		fullPos.y += packet.position[1];
		fullPos.z += packet.position[2];

		fullOrientation.x += ((float)packet.orientation[0]) / 127.0f;
		fullOrientation.y += ((float)packet.orientation[1]) / 127.0f;
		fullOrientation.z += ((float)packet.orientation[2]) / 127.0f;
		fullOrientation.w += ((float)packet.orientation[3]) / 127.0f;

		entity->m_node->SetTranslation(fullPos);
		entity->m_node->SetOrientation(fullOrientation);

		return true;
	}

	bool NetworkComponent::ReadFullPacket(ToolKitNetworking::FullPacket& packet) {

		if (packet.fullState.GetNetworkStateID() < lastFullState.GetNetworkStateID()) {
			return false;
		}

		lastFullState = packet.fullState;

		entity->m_node->SetTranslation(lastFullState.GetPosition());
		entity->m_node->SetOrientation(lastFullState.GetOrientation());
		stateHistory.emplace_back(lastFullState);

		return true;

	}

	bool NetworkComponent::WriteDeltaPacket(GamePacket** packet, int stateID) {
		DeltaPacket* deltaPacket = new DeltaPacket();
		NetworkState state;

		// if we cant get network objects state we fail
		if (!GetNetworkState(stateID, state))
			return false;

		// tells packet what state it is a delta of
		deltaPacket->fullID = stateID;
		deltaPacket->objectID = networkID;

		Vec3 currentPos = entity->m_node->GetTranslation();
		Quaternion currentOrientation = entity->m_node->GetOrientation();

		// find difference between current game states orientation + position and the selected states orientation + position
		currentPos -= state.GetPosition();
		currentOrientation -= state.GetOrientation();

		deltaPacket->position[0] = (char)currentPos.x;
		deltaPacket->position[1] = (char)currentPos.y;
		deltaPacket->position[2] = (char)currentPos.z;

		deltaPacket->orientation[0] = (char)(currentOrientation.x * 127.0f);
		deltaPacket->orientation[1] = (char)(currentOrientation.y * 127.0f);
		deltaPacket->orientation[2] = (char)(currentOrientation.z * 127.0f);
		deltaPacket->orientation[3] = (char)(currentOrientation.w * 127.0f);
		*packet = deltaPacket;

		return true;
	}

	bool NetworkComponent::WriteFullPacket(GamePacket** packet) {
		FullPacket* fullPacket = new FullPacket();


		fullPacket->objectID = networkID;
		fullPacket->fullState.SetPosition(entity->m_node->GetTranslation());
		fullPacket->fullState.SetOrientation(entity->m_node->GetOrientation());

		int lastID = lastFullState.GetNetworkStateID();
		fullPacket->fullState.SetNetworkStateID(lastID++);

		stateHistory.emplace_back(fullPacket->fullState);
		*packet = fullPacket;

		return true;

	}
}

