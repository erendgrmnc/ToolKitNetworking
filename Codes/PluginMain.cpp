/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PluginMain.h"
#include "Editor/EditorMetaKeys.h"
#include "NetworkComponent.h"
#include "NetworkManager.h"
#include "ToolKit/Scene.h"


ToolKit::Editor::PluginMain Self;

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
	namespace Editor
	{

		void PluginMain::Init(Main* master) {
			Main::SetProxy(master);
		}

		void PluginMain::Destroy() {}

		void PluginMain::Frame(float deltaTime) {

			if (m_networkManager.get()) {
				m_networkManager->Update(deltaTime);
			}
		}

		void PluginMain::OnLoad(XmlDocumentPtr state) {
			ToolKitNetworking::NetworkComponent::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkComponent:NetworkComponent";
			ToolKitNetworking::NetworkManager::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkManager:NetworkManager";

			GetObjectFactory()->Register<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Register<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnUnload(XmlDocumentPtr state) {
			if (m_networkManager)
			{
				m_networkManager->Stop();
				m_networkManager = nullptr;
			}
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnPlay() {
			TK_LOG("Network plugin onPlay");
			const auto& entities = GetSceneManager()->GetCurrentScene()->GetEntities();

			for (const auto& entity : entities) {
				if (auto networkManager = entity->GetComponent<ToolKitNetworking::NetworkManager>()) {
					m_networkManager = networkManager;
					TK_LOG("Network Manager found");
				}
			}

			if (m_networkManager)
			{
				if (m_networkManager->GetIsStartingAsServerVal()) {
					TK_LOG("Starting as server");
					m_networkManager->StartAsServer(8080);
				}

				else {
					//TODO(erendegrmnc): pass ip address of the real host.
					const std::string ipAddress = "127.0.0.1";
					TK_LOG("Starting as client");
					m_networkManager->StartAsClient(ipAddress, 8080);
				}

			}
		}

		void PluginMain::OnPause() {
			TK_LOG("Network plugin onPause");
		}

		void PluginMain::OnResume() {
			TK_LOG("Network plugin onResume");
		}

		void PluginMain::OnStop()
		{
			TK_LOG("Network plugin onStop");
			if (m_networkManager)
			{
				m_networkManager->Stop();
				m_networkManager = nullptr;
			}
		}

	} // namespace Editor
} // namespace ToolKit
