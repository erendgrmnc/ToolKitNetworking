#pragma once
#include <string>
#include <vector>
#include "NetworkPackets.h"

namespace ToolKit::ToolKitNetworking
{
	class NetworkVariableBase
	{
	public:
		virtual ~NetworkVariableBase() = default;
		virtual void Serialize(PacketStream& stream) = 0;
		virtual void Deserialize(PacketStream& stream) = 0;
		virtual bool IsDirty() const = 0;
		virtual void ResetDirty() = 0;
		virtual const std::string& GetName() const = 0;
	};

	template<typename T>
	class NetworkVariable : public NetworkVariableBase
	{
	public:
		NetworkVariable(const std::string& name, T defaultValue = T())
			: m_name(name), m_value(defaultValue), m_dirty(true) {}

		T Get() const { return m_value; }
		
		void Set(T val)
		{
			if (m_value != val)
			{
				m_value = val;
				m_dirty = true;
			}
		}

		NetworkVariable& operator=(T val)
		{
			Set(val);
			return *this;
		}

		operator T() const { return m_value; }

		void Serialize(PacketStream& stream) override
		{
			stream.Write(m_value);
		}

		void Deserialize(PacketStream& stream) override
		{
			stream.Read(m_value);
		}

		bool IsDirty() const override { return m_dirty; }
		void ResetDirty() override { m_dirty = false; }
		const std::string& GetName() const override { return m_name; }

	private:
		std::string m_name;
		T m_value;
		bool m_dirty;
	};
}
