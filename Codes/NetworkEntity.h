#pragma once
#include <vector>

#include "NetworkState.h"

namespace ToolKit {
	class Entity;
}

namespace ToolKitNetworking {
	struct FullPacket;
	struct DeltaPacket;
	class NetworkState;
	struct GamePacket;
}

class NetworkEntity {

public:
	NetworkEntity(ToolKit::Entity& entity, int id);
	virtual ~NetworkEntity();

	virtual bool ReadPacket(ToolKitNetworking::GamePacket& packet);
	virtual bool WritePacket(ToolKitNetworking::GamePacket** packet, bool deltaFrame, int stateID);

	int GetNetworkID() const;
	void UpdateStateHistory(int minID);

	ToolKitNetworking::NetworkState& GetLatestNetworkState();
	void SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState);
protected:

	bool GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state);

	virtual bool ReadDeltaPacket(ToolKitNetworking::DeltaPacket& packet);
	virtual bool ReadFullPacket(ToolKitNetworking::FullPacket& packet);

	virtual bool WriteDeltaPacket(ToolKitNetworking::GamePacket** packet, int stateID);
	virtual bool WriteFullPacket(ToolKitNetworking::GamePacket** packet);

	ToolKit::Entity& entity;

	int deltaErrors = 0;
	int fullErrors = 0;
	int networkID;

	ToolKitNetworking::NetworkState lastFullState;

	std::vector<ToolKitNetworking::NetworkState> stateHistory;

};
