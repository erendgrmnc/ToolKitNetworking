#pragma once
#include <Component.h>
#include <vector>
#include "NetworkState.h"

namespace ToolKit
{
	class Entity;

	namespace ToolKitNetworking
	{
		struct FullPacket;
		struct DeltaPacket;
		class NetworkState;
		struct GamePacket;

		typedef std::shared_ptr<class NetworkComponent> NetworkComponentPtr;
		typedef std::vector<NetworkComponentPtr> NetworkComponentPtrArray;

		static VariantCategory NetworkComponentCategory{ "Network Component", 90 };

		class TK_PLUGIN_API NetworkComponent : public ToolKit::Component
		{

		public:
			TKDeclareClass(NetworkComponent, Component)

			virtual void InitNetworkObject(ToolKit::Entity& entity, int id);

			virtual bool ReadPacket(ToolKitNetworking::GamePacket& packet);
			virtual bool WritePacket(ToolKitNetworking::GamePacket** packet, bool deltaFrame, int stateID);

			int GetNetworkID() const;
			void UpdateStateHistory(int minID);

			NetworkState& GetLatestNetworkState();
			void SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState);

			ComponentPtr Copy(EntityPtr ntt) override;

		protected:
			bool GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state);
			virtual bool ReadDeltaPacket(ToolKitNetworking::DeltaPacket& packet);
			virtual bool ReadFullPacket(ToolKitNetworking::FullPacket& packet);

			virtual bool WriteDeltaPacket(ToolKitNetworking::GamePacket** packet, int stateID);
			virtual bool WriteFullPacket(ToolKitNetworking::GamePacket** packet);

			ToolKit::Entity* entity = nullptr;

			int deltaErrors = 0;
			int fullErrors = 0;
			int networkID = -1;

			ToolKitNetworking::NetworkState lastFullState;

			std::vector<ToolKitNetworking::NetworkState> stateHistory;
		};
	}

}
