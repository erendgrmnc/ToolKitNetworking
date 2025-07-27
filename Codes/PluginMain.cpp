/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PluginMain.h"
#include "Editor/EditorMetaKeys.h"
#include "NetworkComponent.h"

ToolKit::Editor::PluginMain Self;

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
  namespace Editor
  {

    void PluginMain::Init(Main* master) { Main::SetProxy(master); }

    void PluginMain::Destroy() {}

    void PluginMain::Frame(float deltaTime) {}

    void PluginMain::OnLoad(XmlDocumentPtr state) {
        ToolKitNetworking::NetworkComponent::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkComponent:NetworkComponent";
    	GetObjectFactory()->Register<ToolKitNetworking::NetworkComponent>();
    }

    void PluginMain::OnUnload(XmlDocumentPtr state) {
        GetObjectFactory()->Unregister<ToolKitNetworking::NetworkComponent>();
    }

  } // namespace Editor
} // namespace ToolKit
