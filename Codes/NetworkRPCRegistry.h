#pragma once
#include <map>
#include <functional>
#include <string>
#include "NetworkPackets.h"

namespace ToolKit
{
	struct ClassMeta;
}

namespace ToolKit::ToolKitNetworking
{
	class NetworkComponent;

	typedef std::function<void(NetworkComponent*, PacketStream&)> RPCDispatcherFn;

	class NetworkRPCRegistry
	{
	public:
		static NetworkRPCRegistry& Instance()
		{
			static NetworkRPCRegistry instance;
			return instance;
		}

		void Register(ToolKit::ClassMeta* cls, const std::string& name, RPCDispatcherFn dispatcher)
		{
			m_registry[cls][CalculateHash(name)] = dispatcher;
		}

		RPCDispatcherFn GetDispatcher(ToolKit::ClassMeta* cls, uint32_t hash)
		{
			if (m_registry.count(cls) && m_registry[cls].count(hash))
			{
				return m_registry[cls][hash];
			}
			
			// Try base classes
			// ToolKit ClassMeta has Super pointer
			// We might need to iterate up the chain
			return nullptr;
		}

		uint32_t CalculateHash(const std::string& name)
		{
			uint32_t hash = 5381;
			for (char c : name)
				hash = ((hash << 5) + hash) + c;
			return hash;
		}

	private:
		std::map<ToolKit::ClassMeta*, std::map<uint32_t, RPCDispatcherFn>> m_registry;
	};
}
