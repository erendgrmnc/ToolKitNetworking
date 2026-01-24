#pragma once
#include <tuple>
#include "NetworkRPCRegistry.h"

#ifdef TK_NET_EXPORT
    #define TK_NET_API __declspec(dllexport)
#else
    #define TK_NET_API __declspec(dllimport)
#endif

namespace ToolKit::ToolKitNetworking
{
	template<typename... Args>
	struct RPCArgUnpacker
	{
		static std::tuple<Args...> Unpack(PacketStream& stream)
		{
			std::tuple<Args...> args;
			std::apply([&stream](auto&... arg) {
				(stream.Read(arg), ...);
			}, args);
			return args;
		}
	};

	template<typename T, typename... Args>
	void RPCDispatcherHelper(NetworkComponent* comp, void (T::*func)(Args...), PacketStream& stream)
	{
		auto args = RPCArgUnpacker<std::decay_t<Args>...>::Unpack(stream);
		std::apply([comp, func](auto&&... unpackedArgs) {
			(static_cast<T*>(comp)->*func)(unpackedArgs...);
		}, args);
	}

	// Helper to register RPCs
	template<typename T, typename... Args>
	struct RPCRegisterer
	{
		RPCRegisterer(ToolKit::ClassMeta* cls, const std::string& name, void (T::*func)(Args...))
		{
			NetworkRPCRegistry::Instance().Register(cls, name, [func](NetworkComponent* comp, PacketStream& stream) {
				RPCDispatcherHelper(comp, func, stream);
			});
		}
	};
}

// Macros for Header
#define TK_RPC_SERVER(Name, ...) \
	void Name(__VA_ARGS__); \
	void Name##_Implementation(__VA_ARGS__)

#define TK_RPC_CLIENT(Name, ...) \
	void Name(__VA_ARGS__); \
	void Name##_Implementation(__VA_ARGS__)

#define TK_RPC_MULTICAST(Name, ...) \
	void Name(__VA_ARGS__); \
	void Name##_Implementation(__VA_ARGS__)

// Macros for Implementation
#define TK_RPC_SERVER_IMPL(Class, Name, Signature, Args) \
	void Class::Name Signature { \
		if (IsServer()) { Name##_Implementation Args; } \
		else { SendRPC(#Name, RPCReceiver::Server, Args); } \
	} \
	static ToolKit::ToolKitNetworking::RPCRegisterer<Class, void Class::*Signature> _rpc_reg_##Class##_##Name(Class::StaticClass(), #Name, &Class::Name##_Implementation); \
	void Class::Name##_Implementation Signature

// Note: The above macro for Registry deduction is tricky. 
// Signature is like (float force). Args is like (force).
// We need to pass the function pointer.
// Corrected Registration line:
#define TK_RPC_SERVER_IMPL_FIXED(Class, Name, Signature, Params) \
	void Class::Name Signature { \
		if (IsServer()) { Name##_Implementation Params; } \
		else { SendRPC(#Name, RPCReceiver::Server, Params); } \
	} \
	void Class::Name##_Implementation Signature

// Actually I can simplify. The user can just call a registration macro in the constructor if they want, 
// OR we use the static helper. The static helper needs to know the type of the function.

// Let's use a more robust version that doesn't require the user to repeat too much.
// Header:
// TK_RPC_SERVER(RequestJump, float force);
// Implementation:
// TK_RPC_SERVER_IMPL(Player, RequestJump, (float force), (force)) { ... }

#undef TK_RPC_SERVER_IMPL
#define TK_RPC_SERVER_IMPL(Class, Name, Signature, Params) \
	void Class::Name Signature { \
		if (IsServer()) { Name##_Implementation Params; } \
		else { SendRPC(#Name, RPCReceiver::Server, Params); } \
	} \
	inline static ToolKit::ToolKitNetworking::RPCRegisterer<Class> _rpc_reg_##Class##_##Name##_instance = \
		ToolKit::ToolKitNetworking::RPCRegisterer<Class>(Class::StaticClass(), #Name, &Class::Name##_Implementation); \
	void Class::Name##_Implementation Signature

// Error in above: RPCRegisterer needs to deduce types.
// I'll use a factory function.
namespace ToolKit::ToolKitNetworking {
	template<typename T, typename... Args>
	RPCRegisterer<T, Args...> MakeRPCRegisterer(ToolKit::ClassMeta* cls, const std::string& name, void (T::*func)(Args...)) {
		return RPCRegisterer<T, Args...>(cls, name, func);
	}
}

#undef TK_RPC_SERVER_IMPL
#define TK_RPC_SERVER_IMPL(Class, Name, Signature, Params) \
	void Class::Name Signature { \
		if (IsServer()) { Name##_Implementation Params; } \
		else { SendRPC(#Name, RPCReceiver::Server, Params); } \
	} \
	static auto _rpc_reg_##Class##_##Name = ToolKit::ToolKitNetworking::MakeRPCRegisterer(Class::StaticClass(), #Name, &Class::Name##_Implementation); \
	void Class::Name##_Implementation Signature

#define TK_RPC_CLIENT_IMPL(Class, Name, Signature, Params) \
	void Class::Name Signature { \
		if (IsServer()) { SendRPC(#Name, RPCReceiver::Owner, Params); } \
		else { Name##_Implementation Params; } \
	} \
	static auto _rpc_reg_##Class##_##Name = ToolKit::ToolKitNetworking::MakeRPCRegisterer(Class::StaticClass(), #Name, &Class::Name##_Implementation); \
	void Class::Name##_Implementation Signature

#define TK_RPC_MULTICAST_IMPL(Class, Name, Signature, Params) \
	void Class::Name Signature { \
		if (IsServer()) { \
			Name##_Implementation Params; \
			SendRPC(#Name, RPCReceiver::Others, Params); \
		} else { \
			SendRPC(#Name, RPCReceiver::Server, Params); \
		} \
	} \
	static auto _rpc_reg_##Class##_##Name = ToolKit::ToolKitNetworking::MakeRPCRegisterer(Class::StaticClass(), #Name, &Class::Name##_Implementation); \
	void Class::Name##_Implementation Signature
