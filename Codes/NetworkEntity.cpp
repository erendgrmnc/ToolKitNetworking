#include "NetworkEntity.h"

#include <Entity.h>

#include "NetworkPackets.h"

NetworkEntity::NetworkEntity(ToolKit::Entity& entity, int id) : entity(entity) {
	networkID = id;
}

NetworkEntity::~NetworkEntity() {
}

bool NetworkEntity::ReadPacket(ToolKitNetworking::GamePacket& packet) {
	if (packet.type == ToolKitNetworking::NetworkMessage::DeltaState)
		return ReadDeltaPacket((ToolKitNetworking::DeltaPacket&)packet);

	if (packet.type == ToolKitNetworking::NetworkMessage::FullState)
		return ReadFullPacket((ToolKitNetworking::FullPacket&)packet);
}

bool NetworkEntity::WritePacket(ToolKitNetworking::GamePacket** packet, bool deltaFrame, int stateID) {
	if (deltaFrame) {
		if (!WriteDeltaPacket(packet, stateID)) {
			return WriteFullPacket(packet);
		}
		return true;
	}
	return WriteFullPacket(packet);
}

int NetworkEntity::GetNetworkID() const {
	return networkID;
}

void NetworkEntity::UpdateStateHistory(int minID) {
	for (auto i = stateHistory.begin(); i < stateHistory.end();) {
		if ((*i).GetNetworkStateID() < minID) {
			//std::cout << "Removing State: " << i->stateID << "\n";
			i = stateHistory.erase(i);
		}
		else
			++i;
	}
}

ToolKitNetworking::NetworkState& NetworkEntity::GetLatestNetworkState() {
	return lastFullState;
}

void NetworkEntity::SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState) {
	lastFullState = lastState;
	stateHistory.push_back(lastFullState);
}

bool NetworkEntity::GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state) {
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

bool NetworkEntity::ReadDeltaPacket(ToolKitNetworking::DeltaPacket& packet) {
	if (packet.fullID != lastFullState.GetNetworkStateID())
		return false;
	UpdateStateHistory(packet.fullID);

	ToolKit::Vec3 fullPos = lastFullState.GetPosition();

	ToolKit::Quaternion fullOrientation = lastFullState.GetOrientation();

	fullPos.x += packet.position[0];
	fullPos.y += packet.position[1];
	fullPos.z += packet.position[2];

	fullOrientation.x += ((float)packet.orientation[0]) / 127.0f;
	fullOrientation.y += ((float)packet.orientation[1]) / 127.0f;
	fullOrientation.z += ((float)packet.orientation[2]) / 127.0f;
	fullOrientation.w += ((float)packet.orientation[3]) / 127.0f;

	entity.m_node->SetTranslation(fullPos);
	entity.m_node->SetOrientation(fullOrientation);

	return true;
}

bool NetworkEntity::ReadFullPacket(ToolKitNetworking::FullPacket& packet) {

	if (packet.fullState.GetNetworkStateID() < lastFullState.GetNetworkStateID()) {
		return false;
	}

	lastFullState = packet.fullState;

	entity.m_node->SetTranslation(lastFullState.GetPosition());
	entity.m_node->SetOrientation(lastFullState.GetOrientation());
	stateHistory.emplace_back(lastFullState);

	return true;

}

bool NetworkEntity::WriteDeltaPacket(ToolKitNetworking::GamePacket** packet, int stateID) {
	ToolKitNetworking::DeltaPacket* deltaPacket = new ToolKitNetworking::DeltaPacket();
	ToolKitNetworking::NetworkState state;

	// if we cant get network objects state we fail
	if (!GetNetworkState(stateID, state))
		return false;

	// tells packet what state it is a delta of
	deltaPacket->fullID = stateID;
	deltaPacket->objectID = networkID;

	ToolKit::Vec3 currentPos = entity.m_node->GetTranslation();
	ToolKit::Quaternion currentOrientation = entity.m_node->GetOrientation();

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

bool NetworkEntity::WriteFullPacket(ToolKitNetworking::GamePacket** packet) {
	ToolKitNetworking::FullPacket* fullPacket = new ToolKitNetworking::FullPacket();


	fullPacket->objectID = networkID;
	fullPacket->fullState.SetPosition(entity.m_node->GetTranslation());
	fullPacket->fullState.SetOrientation(entity.m_node->GetOrientation());

	int lastID = lastFullState.GetNetworkStateID();
	fullPacket->fullState.SetNetworkStateID(lastID++);

	stateHistory.emplace_back(fullPacket->fullState);
	*packet = fullPacket;

	return true;

}
