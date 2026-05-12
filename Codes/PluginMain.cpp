/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PluginMain.h"

#include "Editor/App.h"
#include "Editor/EditorRuntimeLauncher.h"
#include "EditorNetworkPlayPlanner.h"
#include "Editor/EditorScene.h"
#include "Editor/EditorMetaKeys.h"
#include "Editor/Workspace.h"
#include "NetworkComponent.h"
#include "NetworkManager.h"
#include "NetworkSessionCore.h"
#include "ToolKit/Scene.h"

#include <PluginManager.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <Common/Win32Utils.h>
#include <Windows.h>
#endif

ToolKit::Editor::PluginMain Self;

extern "C" TK_PLUGIN_API ToolKit::Plugin* TK_STDCAL GetInstance() { return &Self; }

namespace ToolKit
{
	namespace Editor
	{
    namespace
    {
      using namespace ToolKitNetworking;

      bool CopyConfigDirectory(const String& sourceDir, const String& targetDir)
      {
        try
        {
          const std::filesystem::path sourcePath(sourceDir);
          const std::filesystem::path targetPath(targetDir);
          std::filesystem::create_directories(targetPath);

          if (!std::filesystem::exists(sourcePath))
          {
            return true;
          }

          std::filesystem::copy(sourcePath,
                                targetPath,
                                std::filesystem::copy_options::recursive |
                                    std::filesystem::copy_options::overwrite_existing);
          return true;
        }
        catch (...)
        {
          return false;
        }
      }

      StringArray CollectRuntimePluginNames()
      {
        StringArray pluginNames;
        if (PluginManager* pluginManager = GetPluginManager())
        {
          for (const PluginRegister& plugin : pluginManager->GetRegisteredPlugins())
          {
            if (plugin.m_plugin != nullptr && plugin.m_loaded &&
                plugin.m_plugin->GetType() != PluginType::Editor)
            {
              pluginNames.push_back(plugin.m_name);
            }
          }
        }

        std::sort(pluginNames.begin(), pluginNames.end());
        pluginNames.erase(std::unique(pluginNames.begin(), pluginNames.end()), pluginNames.end());
        return pluginNames;
      }

      String EscapeEngineSettingsXml(const String& value)
      {
        String escaped;
        escaped.reserve(value.size());
        for (const char ch : value)
        {
          switch (ch)
          {
          case '&':
            escaped += "&amp;";
            break;
          case '<':
            escaped += "&lt;";
            break;
          case '>':
            escaped += "&gt;";
            break;
          case '"':
            escaped += "&quot;";
            break;
          case '\'':
            escaped += "&apos;";
            break;
          default:
            escaped.push_back(ch);
            break;
          }
        }
        return escaped;
      }

      bool RewriteChildPluginBlock(const String& settingsPath,
                                   const StringArray& runtimePluginNames)
      {
        std::ifstream input(settingsPath, std::ios::in | std::ios::binary);
        if (!input.is_open())
        {
          return false;
        }

        std::stringstream buffer;
        buffer << input.rdbuf();
        String xml = buffer.str();
        input.close();

        String pluginBlock;
        pluginBlock += "\t<Plugins>\n";
        for (const String& pluginName : runtimePluginNames)
        {
          pluginBlock += "\t\t<Plugin name=\"";
          pluginBlock += EscapeEngineSettingsXml(pluginName);
          pluginBlock += "\"/>\n";
        }
        pluginBlock += "\t</Plugins>";

        const String openTag = "<Plugins>";
        const String closeTag = "</Plugins>";
        const size_t openPos = xml.find(openTag);
        if (openPos != String::npos)
        {
          const size_t closePos = xml.find(closeTag, openPos);
          if (closePos == String::npos)
          {
            return false;
          }

          xml.replace(openPos, closePos + closeTag.size() - openPos, pluginBlock);
        }
        else
        {
          const size_t settingsEnd = xml.rfind("</Settings>");
          if (settingsEnd == String::npos)
          {
            return false;
          }
          xml.insert(settingsEnd, pluginBlock + "\n");
        }

        std::ofstream output(settingsPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
          return false;
        }
        output << xml;
        return true;
      }

      bool WriteNetworkPlaySceneSnapshot(const String& sceneSnapshotPath,
                                         String& errorMessage)
      {
        EditorScenePtr currentScene = Cast<EditorScene>(GetSceneManager()->GetCurrentScene());
        if (!currentScene)
        {
          errorMessage = "Failed to capture the current editor scene for child boot.";
          return false;
        }

        try
        {
          std::filesystem::create_directories(
              std::filesystem::path(sceneSnapshotPath).parent_path());
        }
        catch (...)
        {
          errorMessage = "Failed to create the child scene snapshot directory.";
          return false;
        }

        std::ofstream sceneFile(sceneSnapshotPath, std::ios::out | std::ios::trunc);
        if (!sceneFile.is_open())
        {
          errorMessage = "Failed to open the child scene snapshot for writing.";
          return false;
        }

        XmlDocument doc;
        currentScene->Serialize(&doc, nullptr);
        std::string xml;
        rapidxml::print(std::back_inserter(xml), doc, 0);
        sceneFile << xml;
        sceneFile.close();
        return true;
      }

      bool WriteChildRuntimeEngineSettings(const String& configRoot,
                                           const StringArray& runtimePluginNames,
                                           String& errorMessage)
      {
        try
        {
          std::filesystem::create_directories(configRoot);
        }
        catch (...)
        {
          errorMessage = "Failed to create the child config directory.";
          return false;
        }

        EngineSettings& settings = GetEngineSettings();
        const StringSet previousPlugins = settings.m_loadedPlugins;
        settings.m_loadedPlugins.clear();
        for (const String& pluginName : runtimePluginNames)
        {
          settings.m_loadedPlugins.insert(pluginName);
        }

        const String settingsPath = ConcatPaths({configRoot, "Engine.settings"});
        settings.Save(settingsPath);
        settings.m_loadedPlugins = previousPlugins;
        if (!RewriteChildPluginBlock(settingsPath, runtimePluginNames))
        {
          errorMessage = "Failed to author the child runtime plugin settings.";
          return false;
        }
        return true;
      }

      bool WriteNetworkPlayManifest(const String& manifestPath,
                                    const NetworkPlaySessionSpec& session,
                                    const NetworkPlayInstanceSpec& instance,
                                    String& errorMessage)
      {
        try
        {
          std::filesystem::create_directories(
              std::filesystem::path(manifestPath).parent_path());
        }
        catch (...)
        {
          errorMessage = "Failed to create the manifest directory.";
          return false;
        }

        const String configRoot =
            ConcatPaths({session.launchRoot, "Config", instance.instanceId});
        const String tempRoot =
            ConcatPaths({session.launchRoot, "Temp", instance.instanceId});
        const String logRoot =
            ConcatPaths({session.launchRoot, "Logs", instance.instanceId});
        if (!CopyConfigDirectory(session.configTemplateRoot, configRoot))
        {
          errorMessage = "Failed to prepare isolated child config files.";
          return false;
        }
        if (!WriteChildRuntimeEngineSettings(configRoot, session.runtimePluginNames, errorMessage))
        {
          return false;
        }
        if (!WriteNetworkPlaySceneSnapshot(session.sceneSnapshotPath, errorMessage))
        {
          return false;
        }

        try
        {
          std::filesystem::create_directories(tempRoot);
          std::filesystem::create_directories(logRoot);
        }
        catch (...)
        {
          errorMessage = "Failed to create the child temp/log directories.";
          return false;
        }

        std::ofstream manifestFile(manifestPath, std::ios::out | std::ios::trunc);
        if (!manifestFile.is_open())
        {
          errorMessage = "Failed to open the child manifest for writing.";
          return false;
        }
        manifestFile << BuildNetworkPlayManifestXml(session, instance, configRoot);
        manifestFile.close();
        return true;
      }

      bool CollectSceneNetworkManagers(NetworkManagerPtrArray& managers)
      {
        managers.clear();
        ScenePtr scene = GetSceneManager()->GetCurrentScene();
        if (scene == nullptr)
        {
          return false;
        }

        const auto& entities = scene->GetEntities();
        for (const auto& entity : entities)
        {
          if (auto manager = entity->GetComponent<NetworkManager>())
          {
            managers.push_back(manager);
          }
        }

        return true;
      }

      bool ConfigureLocalNetworkManager(NetworkManager& manager,
                                        PluginMain::NetworkManagerOverrideState& snapshot,
                                        const NetworkPlayInstanceSpec& instance)
      {
        if (!snapshot.active)
        {
          snapshot.active = true;
          snapshot.role = manager.GetRoleVal();
          snapshot.connectHost = manager.GetConnectHostVal();
          snapshot.connectPort = manager.GetConnectPortVal();
          snapshot.listenPort = manager.GetListenPortVal();
          snapshot.bindAddress = manager.GetBindAddressVal();
          snapshot.advertisedAddress = manager.GetAdvertisedAddressVal();
          snapshot.maxClients = manager.GetMaxClientsVal();
        }

        MultiChoiceVariant role = manager.GetRoleVal();
        switch (instance.hostingMode)
        {
        case HostingMode::ListenServer:
          role.SetEnum(NetworkRole::Host);
          break;
        case HostingMode::DedicatedServer:
          role.SetEnum(NetworkRole::DedicatedServer);
          break;
        case HostingMode::Client:
        default:
          role.SetEnum(NetworkRole::Client);
          break;
        }

        manager.SetRoleVal(role);
        manager.SetConnectHostVal(instance.connectHost);
        manager.SetConnectPortVal(instance.connectPort);
        manager.SetListenPortVal(instance.listenPort);
        manager.SetBindAddressVal(instance.bindAddress);
        manager.SetAdvertisedAddressVal(instance.advertisedAddress);
        if (instance.maxClients != 0)
        {
          manager.SetMaxClientsVal(instance.maxClients);
        }

        return true;
      }
    } // namespace

		void PluginMain::Init(Main* master)
    {
			Main::SetProxy(master);
      TK_LOG("Network plugin Init");
		}

		void PluginMain::Destroy()
    {
      TerminateTrackedChildProcesses(true);
      RestoreNetworkManagerOverrides();
    }

		void PluginMain::Frame(float deltaTime)
    {
      if (m_abortEditorPlayRequested)
      {
        m_abortEditorPlayRequested = false;
        if (m_networkManager)
        {
          StopLocalConfiguredSession();
          RestoreNetworkManagerOverrides();
          m_networkManager = nullptr;
        }
        else
        {
          RestoreNetworkManagerOverrides();
        }
        TerminateTrackedChildProcesses(true);
        if (!m_abortEditorPlayReason.empty())
        {
          TK_ERR("%s", m_abortEditorPlayReason.c_str());
        }
        return;
      }

      PollChildProcesses();

			if (m_networkManager)
      {
				m_networkManager->Update(deltaTime);
			}
		}

		void PluginMain::OnLoad(XmlDocumentPtr state)
    {
      (void) state;
      TK_LOG("Network plugin OnLoad");
			ToolKitNetworking::NetworkComponent::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkComponent:NetworkComponent";
			ToolKitNetworking::NetworkManager::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkManager:NetworkManager";

			GetObjectFactory()->Register<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Register<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnUnload(XmlDocumentPtr state)
    {
      (void) state;
      TerminateTrackedChildProcesses(true);
      if (m_networkManager)
			{
				StopLocalConfiguredSession();
        RestoreNetworkManagerOverrides();
				m_networkManager = nullptr;
			}
      else
      {
        RestoreNetworkManagerOverrides();
      }
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnPlay()
    {
      TK_LOG("Network plugin OnPlay");
      m_abortEditorPlayRequested = false;
      m_abortEditorPlayReason.clear();
      m_keepChildrenAliveAfterStop = false;

      if (IsEditorNetworkPlayEnabled())
      {
        TK_LOG("Network plugin starting editor network play session");
        if (!StartNetworkPlaySession())
        {
          return;
        }
        return;
      }

      TK_LOG("Network plugin starting single-process network session");
      StartSingleProcessSession();
		}

		void PluginMain::OnPause()
    {
			TK_LOG("Network plugin onPause");
		}

		void PluginMain::OnResume()
    {
			TK_LOG("Network plugin onResume");
		}

		void PluginMain::OnStop()
		{
			TK_LOG("Network plugin onStop");
			if (m_networkManager)
			{
				StopLocalConfiguredSession();
        RestoreNetworkManagerOverrides();
				m_networkManager = nullptr;
			}
      else
      {
        RestoreNetworkManagerOverrides();
      }
      TerminateTrackedChildProcesses(!m_keepChildrenAliveAfterStop);
      m_keepChildrenAliveAfterStop = false;
		}

    void PluginMain::PollChildProcesses()
    {
      for (auto it = m_childProcesses.begin(); it != m_childProcesses.end();)
      {
        if (it->process.processHandle == 0)
        {
          it = m_childProcesses.erase(it);
          continue;
        }

        if (!IsChildProcessActive(*it))
        {
          ReleaseChildProcess(*it, false);
          it = m_childProcesses.erase(it);
          continue;
        }

        ++it;
      }
    }

    void PluginMain::RestoreNetworkManagerOverrides()
    {
      if (!m_overrideState.active || !m_networkManager)
      {
        m_overrideState = NetworkManagerOverrideState {};
        return;
      }

      m_networkManager->SetRoleVal(m_overrideState.role);
      m_networkManager->SetConnectHostVal(m_overrideState.connectHost);
      m_networkManager->SetConnectPortVal(m_overrideState.connectPort);
      m_networkManager->SetListenPortVal(m_overrideState.listenPort);
      m_networkManager->SetBindAddressVal(m_overrideState.bindAddress);
      m_networkManager->SetAdvertisedAddressVal(m_overrideState.advertisedAddress);
      m_networkManager->SetMaxClientsVal(m_overrideState.maxClients);
      m_overrideState = NetworkManagerOverrideState {};
    }

    void PluginMain::TerminateTrackedChildProcesses(bool force)
    {
      for (ChildProcessInfo& child : m_childProcesses)
      {
        ReleaseChildProcess(child, force);
      }
      m_childProcesses.clear();
    }

    bool PluginMain::IsEditorNetworkPlayEnabled() const
    {
      return GetApp() != nullptr && GetApp()->m_networkPlaySettings.Enabled;
    }

    bool PluginMain::ResolveNetworkPlayStartContext(
        NetworkPlayStartContext& context,
        String& errorMessage)
    {
#ifndef _WIN32
      errorMessage =
          "Editor network play currently supports Windows child launches only.";
      return false;
#else
      NetworkManagerPtrArray managers;
      if (!CollectSceneNetworkManagers(managers))
      {
        errorMessage = "No active scene is available for editor network play.";
        return false;
      }
      if (managers.size() != 1)
      {
        errorMessage =
            "Editor network play requires exactly one active NetworkManager in the scene.";
        return false;
      }

      context.networkManager = managers.front();
      if (!context.networkManager)
      {
        errorMessage =
            "Editor network play could not acquire the scene NetworkManager.";
        return false;
      }

      const SessionBootstrapConfig sessionConfig =
          context.networkManager->GetSessionBootstrapConfig();
      if (sessionConfig.joinMethod != JoinMethod::DirectAddress)
      {
        errorMessage =
            "Editor network play currently supports DirectAddress only.";
        return false;
      }

      if (GetApp() == nullptr)
      {
        errorMessage = "Editor network play requires an active editor app.";
        return false;
      }

      const Project activeProject = GetApp()->m_workspace.GetActiveProject();
      if (activeProject.name.empty())
      {
        errorMessage = "Editor network play requires an active project.";
        return false;
      }

      EditorScenePtr currentScene =
          Cast<EditorScene>(GetSceneManager()->GetCurrentScene());
      if (!currentScene)
      {
        errorMessage = "Editor network play requires an active scene.";
        return false;
      }

      const String scenePath = currentScene->GetFile();
      if (scenePath.empty() || !CheckFile(scenePath))
      {
        errorMessage =
            "Editor network play requires the current scene to be saved to disk.";
        return false;
      }

      const String workspaceRoot = GetApp()->m_workspace.GetActiveWorkspace();
      const String projectRoot =
          ConcatPaths({workspaceRoot, activeProject.name});
      const String executablePath =
          GetEditorRuntimeExecutablePath(GetApp()->m_workspace);
      if (!CheckFile(executablePath))
      {
        errorMessage =
            "Editor network play could not find the standalone runtime executable.";
        return false;
      }

      const NetworkPlaySettings& settings = GetApp()->m_networkPlaySettings;
      context.autoStopChildren = settings.AutoStopChildren;
      context.plannerSettings.playerCount =
          static_cast<uint>(settings.PlayerCount);
      context.plannerSettings.runDedicatedServerHeadless =
          settings.RunDedicatedServerHeadless;
      context.plannerSettings.basePort = settings.BasePort;
      context.plannerSettings.autoAllocatePorts = settings.AutoAllocatePorts;
      context.plannerSettings.topology = settings.Topology;

      context.sceneConfig.joinMethod = sessionConfig.joinMethod;
      context.sceneConfig.maxClients =
          context.networkManager->GetMaxClientsVal();
      context.sceneConfig.connectHost =
          context.networkManager->GetConnectHostVal();
      context.sceneConfig.connectPort = static_cast<uint16_t>(
          context.networkManager->GetConnectPortVal());
      context.sceneConfig.listenPort = static_cast<uint16_t>(
          context.networkManager->GetListenPortVal());
      context.sceneConfig.bindAddress =
          context.networkManager->GetBindAddressVal();
      context.sceneConfig.advertisedAddress =
          context.networkManager->GetAdvertisedAddressVal();

      context.metadata.launchId = BuildNetworkPlayLaunchId();
      context.metadata.launchRoot = ConcatPaths(
          {projectRoot, "Intermediate", "NetworkPlay", context.metadata.launchId});
      context.metadata.projectRoot = projectRoot;
      context.metadata.workspaceRoot = workspaceRoot;
      context.metadata.configTemplateRoot =
          GetApp()->m_workspace.GetConfigDirectory();
      context.metadata.resourceRoot = GetApp()->m_workspace.GetResourceRoot();
      context.metadata.scenePath = scenePath;
      context.metadata.executablePath = executablePath;
      context.metadata.runtimePluginNames = CollectRuntimePluginNames();
      return true;
#endif
    }

    bool PluginMain::WriteChildManifestFile(const String& manifestPath,
                                            const NetworkPlaySessionSpec& session,
                                            const NetworkPlayInstanceSpec& instance,
                                            String& errorMessage)
    {
      return WriteNetworkPlayManifest(manifestPath, session, instance, errorMessage);
    }

    bool PluginMain::LaunchChildProcess(const String& executablePath,
                                        const String& manifestPath,
                                        const NetworkPlayInstanceSpec& instance,
                                        ChildProcessInfo& childProcess,
                                        String& errorMessage)
    {
      EditorRuntimeLaunchRequest request;
      request.executablePath = executablePath;
      request.manifestPath = manifestPath;
      request.roleFlag = GetNetworkPlayRoleFlag(instance.hostingMode);
      request.connectHost = instance.connectHost;
      request.connectPort = instance.connectPort;
      request.listenPort = instance.listenPort;
      request.bindAddress = instance.bindAddress;
      request.advertisedAddress = instance.advertisedAddress;
      request.headless = instance.headless;

      TK_LOG(("Network plugin launching child executable=" + executablePath +
              " manifest=" + manifestPath + " role=" + instance.roleName)
                 .c_str());
      if (!LaunchEditorRuntimeProcess(request, childProcess.process, errorMessage))
      {
        return false;
      }

      childProcess.manifestPath = manifestPath;
      childProcess.roleName = instance.roleName;
      return true;
    }

    bool PluginMain::IsChildProcessActive(const ChildProcessInfo& child) const
    {
      return IsEditorRuntimeProcessActive(child.process);
    }

    void PluginMain::ReleaseChildProcess(ChildProcessInfo& child, bool forceTerminate)
    {
      ReleaseEditorRuntimeProcess(child.process, forceTerminate);
    }

    void PluginMain::SleepForChildStartup(uint milliseconds)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    bool PluginMain::StartLocalConfiguredSession()
    {
      if (m_networkManager == nullptr)
      {
        TK_LOG("Network plugin cannot start local session: NetworkManager is null.");
        return false;
      }

      TK_LOG("Network plugin starting local NetworkManager configured session.");
      const bool started = m_networkManager->StartConfiguredSession();
      const auto status = m_networkManager->GetConnectionStatus();
      TK_LOG(("Network plugin local NetworkManager start result=" +
              std::to_string(started ? 1 : 0) + " state=" +
              std::to_string(static_cast<int>(status.state)) + " detail=" +
              status.detailMessage)
                 .c_str());
      return started;
    }

    void PluginMain::StopLocalConfiguredSession()
    {
      if (m_networkManager != nullptr)
      {
        m_networkManager->Stop();
      }
    }

    void PluginMain::RequestAbortPlay(const String& reason)
    {
      TK_ERR("%s", reason.c_str());
      m_abortEditorPlayRequested = true;
      m_abortEditorPlayReason = reason;
    }

    bool PluginMain::StartSingleProcessSession()
    {
      NetworkManagerPtrArray managers;
      if (!CollectSceneNetworkManagers(managers) || managers.empty())
      {
        return true;
      }

      m_networkManager = managers.back();
      return m_networkManager && m_networkManager->StartConfiguredSession();
    }

    bool PluginMain::StartNetworkPlaySession()
    {
      TK_LOG("Network plugin StartNetworkPlaySession entered.");
      TerminateTrackedChildProcesses(true);
      RestoreNetworkManagerOverrides();
      NetworkPlayStartContext context;
      String contextError;
      if (!ResolveNetworkPlayStartContext(context, contextError))
      {
        TK_LOG(("Network plugin failed to resolve network play context: " +
                contextError)
                   .c_str());
        RequestAbortPlay(contextError.empty()
                             ? "Failed to resolve the editor network play context."
                             : contextError);
        return false;
      }

      m_networkManager = context.networkManager;
      NetworkPlaySessionSpec session;
      String planningError;
      if (!BuildNetworkPlaySessionSpec(context.plannerSettings,
                                       context.sceneConfig,
                                       context.metadata,
                                       session,
                                       planningError))
      {
        TK_LOG(("Network plugin failed to build network play session: " +
                planningError)
                   .c_str());
        RequestAbortPlay(planningError.empty()
                             ? "Failed to build the editor network play session."
                             : planningError);
        return false;
      }

      m_keepChildrenAliveAfterStop = !context.autoStopChildren;

      if (context.plannerSettings.topology == NetworkPlayTopology::DedicatedServer &&
          !session.childInstances.empty())
      {
        String errorMessage;
        const NetworkPlayInstanceSpec& serverChild = session.childInstances.front();
        const String manifestPath = ConcatPaths(
            {session.launchRoot, "instances", serverChild.instanceId + ".settings"});

        ChildProcessInfo childProcess;
        if (!WriteChildManifestFile(manifestPath, session, serverChild, errorMessage) ||
            !LaunchChildProcess(context.metadata.executablePath,
                                manifestPath,
                                serverChild,
                                childProcess,
                                errorMessage))
        {
          TK_LOG(("Network plugin failed to launch dedicated server child: " +
                  errorMessage)
                     .c_str());
          RequestAbortPlay(errorMessage.empty()
                               ? "Failed to start the dedicated server child process."
                               : errorMessage);
          TerminateTrackedChildProcesses(true);
          m_networkManager = nullptr;
          return false;
        }

        m_childProcesses.push_back(childProcess);
        SleepForChildStartup(250);
      }

      ConfigureLocalNetworkManager(*m_networkManager, m_overrideState, session.localInstance);
      TK_LOG(("Network plugin configured local NetworkManager role=" +
              session.localInstance.roleName + " connect=" +
              session.localInstance.connectHost + ":" +
              std::to_string(session.localInstance.connectPort) + " listen=" +
              std::to_string(session.localInstance.listenPort))
                 .c_str());
      if (!StartLocalConfiguredSession())
      {
        TerminateTrackedChildProcesses(true);
        RestoreNetworkManagerOverrides();
        RequestAbortPlay("Failed to start the local NetworkManager for editor network play.");
        m_networkManager = nullptr;
        return false;
      }

      if (context.plannerSettings.topology == NetworkPlayTopology::ListenServer)
      {
        SleepForChildStartup(250);
      }

      const size_t childStartIndex =
          context.plannerSettings.topology == NetworkPlayTopology::DedicatedServer ? 1u : 0u;
      for (size_t childIndex = childStartIndex; childIndex < session.childInstances.size();
           ++childIndex)
      {
        const NetworkPlayInstanceSpec& childSpec = session.childInstances[childIndex];
        const String manifestPath = ConcatPaths(
            {session.launchRoot, "instances", childSpec.instanceId + ".settings"});
        String errorMessage;
        ChildProcessInfo childProcess;
        if (!WriteChildManifestFile(manifestPath, session, childSpec, errorMessage) ||
            !LaunchChildProcess(context.metadata.executablePath,
                                manifestPath,
                                childSpec,
                                childProcess,
                                errorMessage))
        {
          TK_LOG(("Network plugin failed to launch child " +
                  childSpec.instanceId + ": " + errorMessage)
                     .c_str());
          StopLocalConfiguredSession();
          TerminateTrackedChildProcesses(true);
          RestoreNetworkManagerOverrides();
          RequestAbortPlay(errorMessage.empty()
                               ? "Failed to start one of the editor network-play child processes."
                               : errorMessage);
          m_networkManager = nullptr;
          return false;
        }

        m_childProcesses.push_back(childProcess);
        TK_LOG(("Network plugin launched child " + childSpec.instanceId +
                " role=" + childSpec.roleName)
                   .c_str());
      }

      TK_LOG("Editor network play launched.");
      return true;
    }

	} // namespace Editor
} // namespace ToolKit
