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

#include "Editor/EditorRuntimeLauncher.h"
#include "EditorNetworkPlayPlanner.h"
#include "NetworkManager.h"

#include <vector>

namespace ToolKit
{
  namespace Editor
  {

    class TK_NET_EDITOR_API PluginMain : public Plugin
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
        EditorChildProcess process;
        String manifestPath;
        String roleName;
      };

      struct NetworkPlayStartContext
      {
        ToolKitNetworking::NetworkManagerPtr networkManager;
        NetworkPlayPlannerSettings plannerSettings;
        NetworkPlaySceneConfig sceneConfig;
        NetworkPlaySessionMetadata metadata;
        bool autoStopChildren = true;
      };

     private:
      void PollChildProcesses();
      void RestoreNetworkManagerOverrides();
      void TerminateTrackedChildProcesses(bool force);
      bool StartNetworkPlaySession();
      bool StartSingleProcessSession();

     protected:
      virtual bool IsEditorNetworkPlayEnabled() const;
      virtual bool ResolveNetworkPlayStartContext(NetworkPlayStartContext& context,
                                                  String& errorMessage);
      virtual bool WriteChildManifestFile(const String& manifestPath,
                                          const NetworkPlaySessionSpec& session,
                                          const NetworkPlayInstanceSpec& instance,
                                          String& errorMessage);
      virtual bool LaunchChildProcess(const String& executablePath,
                                      const String& manifestPath,
                                      const NetworkPlayInstanceSpec& instance,
                                      ChildProcessInfo& childProcess,
                                      String& errorMessage);
      virtual bool IsChildProcessActive(const ChildProcessInfo& child) const;
      virtual void ReleaseChildProcess(ChildProcessInfo& child, bool forceTerminate);
      virtual void SleepForChildStartup(uint milliseconds);
      virtual bool StartLocalConfiguredSession();
      virtual void StopLocalConfiguredSession();
      virtual void RequestAbortPlay(const String& reason);

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
