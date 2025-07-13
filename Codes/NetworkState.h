#pragma once
#include <Types.h>

namespace ToolKitNetworking {
	class NetworkState {
	public:
		NetworkState();

		int GetNetworkStateID() const;
		void SetNetworkStateID(int newStateID);

		ToolKit::Quaternion GetOrientation() const;
		void SetOrientation(ToolKit::Quaternion newOrientation);

		ToolKit::Vec3 GetPosition() const;
		void SetPosition(ToolKit::Vec3 newPosition);

	protected:
		int stateID;

		ToolKit::Vec3 position;
		ToolKit::Quaternion orientation;
	};
}
