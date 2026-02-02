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
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

ToolKit::Editor::PluginMain Self;

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
	namespace Editor
	{
		// Helper to parse command line args
		void ParseCommandLine(ToolKitNetworking::NetworkRole& role, std::string& ip, int& port)
		{
#ifdef _WIN32
			LPSTR cmdLine = GetCommandLineA();
			std::string cmd(cmdLine);
#else
			// Fallback or other platform implementation
			std::string cmd = ""; 
#endif
			// Simple parsing
			std::vector<std::string> args;
			std::string current;
			bool inQuotes = false;
			for (char c : cmd) {
				if (c == ' ' && !inQuotes) {
					if (!current.empty()) {
						args.push_back(current);
						current.clear();
					}
				}
				else if (c == '"') {
					inQuotes = !inQuotes;
				}
				else {
					current += c;
				}
			}
			if (!current.empty()) args.push_back(current);

			for (size_t i = 0; i < args.size(); ++i) {
				if (args[i] == "-server" || args[i] == "-dedicated") {
					role = ToolKitNetworking::NetworkRole::DedicatedServer;
				}
				else if (args[i] == "-host") {
					role = ToolKitNetworking::NetworkRole::Host;
				}
				else if (args[i] == "-client") {
					role = ToolKitNetworking::NetworkRole::Client;
				}
				else if (args[i] == "-ip" && i + 1 < args.size()) {
					ip = args[i + 1];
				}
				else if (args[i] == "-port" && i + 1 < args.size()) {
					port = std::stoi(args[i + 1]);
				}
			}
		}

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
				auto roleVar = m_networkManager->GetRoleVal();
				auto role = roleVar.GetEnum<ToolKitNetworking::NetworkRole>();
				std::string ip = "127.0.0.1";
				int port = 8080;

				ParseCommandLine(role, ip, port);

				auto netRole = role;

				if (netRole == ToolKitNetworking::NetworkRole::DedicatedServer || netRole == ToolKitNetworking::NetworkRole::Host) {
					if (!m_networkManager->IsServer()) {
						m_networkManager->StartAsServer(port);
					}
				}

				if (netRole == ToolKitNetworking::NetworkRole::Client || netRole == ToolKitNetworking::NetworkRole::Host) {
					m_networkManager->StartAsClient(ip, port);
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
