#pragma once
#include <Component.h>
#include <vector>
#include <memory>
#include "NetworkPackets.h"

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

			virtual ~NetworkComponent();

			void SetNetworkID(int id);

			virtual void Serialize(PacketStream& stream, int baseTick);
			virtual void Deserialize(PacketStream& stream, int baseTick);

			void ParameterConstructor() override;

			int GetNetworkID() const;
			EntityPtr GetEntity() const { return m_entity.lock(); }
			void UpdateStateHistory(int minID);

			NetworkState& GetLatestNetworkState();
			void SetLatestNetworkState(ToolKitNetworking::NetworkState& lastState);

			ComponentPtr Copy(EntityPtr entityPtr) override;

		protected:
			bool GetNetworkState(int stateID, ToolKitNetworking::NetworkState& state);

			int deltaErrors = 0;
			int fullErrors = 0;
			int networkID = -1;

			ToolKitNetworking::NetworkState lastFullState;

			std::vector<ToolKitNetworking::NetworkState> stateHistory;
		};
	}

}
