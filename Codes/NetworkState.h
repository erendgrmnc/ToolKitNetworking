#pragma once
#include <Component.h>

namespace ToolKit::ToolKitNetworking {
	class NetworkState {
	public:
		NetworkState();

		int GetNetworkStateID() const;
		void SetNetworkStateID(int newStateID);

		Quaternion GetOrientation() const;
		void SetOrientation(ToolKit::Quaternion newOrientation);

		Vec3 GetPosition() const;
		void SetPosition(ToolKit::Vec3 newPosition);

	protected:
		int stateID;

		Vec3 position;
		Quaternion orientation;
	};
}
