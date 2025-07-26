#include "NetworkState.h"

namespace ToolKit::ToolKitNetworking {
	NetworkState::NetworkState() {
		stateID = 0;
	}

	int NetworkState::GetNetworkStateID() const {

		return stateID;
	}

	void NetworkState::SetNetworkStateID(int newStateID) {
		stateID = newStateID;
	}

	Quaternion NetworkState::GetOrientation() const {
		return orientation;
	}

	void NetworkState::SetOrientation(Quaternion newOrientation) {
		orientation = newOrientation;
	}

	Vec3 NetworkState::GetPosition() const {
		return position;
	}

	void NetworkState::SetPosition(Vec3 newPosition) {
		position = newPosition;
	}

}




