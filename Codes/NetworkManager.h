#pragma once
#include "NetworkBase.h"

namespace ToolKit::ToolKitNetworking{
	class NetworkManager : public EntityNode, public PacketReceiver {
	public:

		void StartAsClient();
		void StartAsServer(char a, char b, char c, char d);

		void ReceivePacket(int type, GamePacket* payload, int source) override;
	protected:

		void UpdateAsServer(float dt);
		void UpdateAsClient(float dt);

		void BroadcastSnapshot(bool isDeltaFrame);
		void UpdateMinimumState();

		std::map<int, int> stateIDs;

		int packetsToSnapshot;
		float timeToNextPacket;

	};
}
