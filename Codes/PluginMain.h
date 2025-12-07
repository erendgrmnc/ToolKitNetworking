/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include <Plugin.h>
#include <ToolKit.h>
#include <enet/enet.h>

namespace ToolKit
{
	namespace ToolKitNetworking {
		class NetworkManager;
	}

	namespace Editor
	{

		class PluginMain : public Plugin
		{
		public:
			PluginType GetType() override { return PluginType::Editor; }

			void Init(Main* master) override;
			void Destroy() override;
			void Frame(float deltaTime) override;
			void OnLoad(XmlDocumentPtr state) override;
			void OnUnload(XmlDocumentPtr state) override;
			void OnPlay() override;
			void OnPause() override;
			void OnResume() override;
			void OnStop() override;

		protected:
			ToolKitNetworking::NetworkManager* m_networkManager;
		};

	} // namespace Editor
} // namespace ToolKit

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance();