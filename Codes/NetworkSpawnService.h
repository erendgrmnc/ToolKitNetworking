#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include "NetworkComponent.h"
#include "NetworkMacros.h" 

namespace ToolKit
{
    namespace ToolKitNetworking
    {
        class TK_NET_API NetworkSpawnService
        {
        public:
            typedef std::function<NetworkComponent* ()> SpawnFactory;

            static NetworkSpawnService& GetInstance();

            void RegisterFactory(const std::string& className, SpawnFactory factory);

            template<typename T>
            void Register()
            {
                static_assert(std::is_base_of<NetworkComponent, T>::value, "T must derive from NetworkComponent");
                std::string name = T::StaticClass()->Name;  
                RegisterFactory(name, []() -> NetworkComponent* {
                    return new T();
                });
            }

            NetworkComponent* Spawn(const std::string& className);
            const std::unordered_map<std::string, SpawnFactory>& GetFactories() const { return m_spawnFactories; }

        private:
            NetworkSpawnService() = default;
            ~NetworkSpawnService() = default;

            std::unordered_map<std::string, SpawnFactory> m_spawnFactories;
        };
    }
}
