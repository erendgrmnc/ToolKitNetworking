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

#include "NetworkManager.h"

#include <vector>

namespace ToolKit
{
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

     public:
      struct NetworkManagerOverrideState
      {
        bool active = false;
        MultiChoiceVariant role;
        String connectHost;
        uint connectPort = 0;
        uint listenPort = 0;
        String bindAddress;
        String advertisedAddress;
        uint maxClients = 0;
      };

      struct ChildProcessInfo
      {
        uintptr_t processHandle = 0;
        unsigned long processId = 0;
        String manifestPath;
        String roleName;
      };

     private:
      void PollChildProcesses();
      void RequestAbortPlay(const String& reason);
      void RestoreNetworkManagerOverrides();
      void TerminateTrackedChildProcesses(bool force);
      bool StartNetworkPlaySession();
      bool StartSingleProcessSession();

     protected:
      ToolKitNetworking::NetworkManagerPtr m_networkManager;
      NetworkManagerOverrideState m_overrideState;
      std::vector<ChildProcessInfo> m_childProcesses;
      bool m_abortEditorPlayRequested = false;
      String m_abortEditorPlayReason;
      bool m_keepChildrenAliveAfterStop = false;
    };

  } // namespace Editor
} // namespace ToolKit

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance();
