#include "NetworkState.h"

ToolKitNetworking::NetworkState::NetworkState() {
	stateID = 0;
}

int ToolKitNetworking::NetworkState::GetNetworkStateID() const {

	return stateID;
}

void ToolKitNetworking::NetworkState::SetNetworkStateID(int newStateID) {
	stateID = newStateID;
}

ToolKit::Quaternion ToolKitNetworking::NetworkState::GetOrientation() const {
	return orientation;
}

void ToolKitNetworking::NetworkState::SetOrientation(ToolKit::Quaternion newOrientation) {
	orientation = newOrientation;
}

ToolKit::Vec3 ToolKitNetworking::NetworkState::GetPosition() const {
	return position;
}

void ToolKitNetworking::NetworkState::SetPosition(ToolKit::Vec3 newPosition) {
	position = newPosition;
}


