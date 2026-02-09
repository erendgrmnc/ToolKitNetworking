#include "NetworkSpawnService.h"
#include "ToolKit.h" 
#include <Entity.h>
#include <Node.h> 

namespace ToolKit
{
    namespace ToolKitNetworking
    {
        NetworkSpawnService& NetworkSpawnService::GetInstance()
        {
            static NetworkSpawnService instance;
            return instance;
        }

        void NetworkSpawnService::RegisterFactory(const std::string& className, SpawnFactory factory)
        {
            if (m_spawnFactories.find(className) != m_spawnFactories.end())
            {
                return;
            }

            m_spawnFactories[className] = factory;
        }
        NetworkComponent* NetworkSpawnService::Spawn(const std::string& className)
        {
            auto it = m_spawnFactories.find(className);
            if (it == m_spawnFactories.end())
            {
                return nullptr;
            }

            NetworkComponent* netComp = it->second();
            if (netComp)
            {
                netComp->SetSpawnClassName(className);
            }

            return netComp;
        }
    }
}
