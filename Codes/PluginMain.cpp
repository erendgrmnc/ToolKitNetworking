/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PluginMain.h"

#include "Editor/App.h"
#include "EditorNetworkPlayPlanner.h"
#include "Editor/EditorScene.h"
#include "Editor/EditorMetaKeys.h"
#include "Editor/Workspace.h"
#include "NetworkComponent.h"
#include "NetworkManager.h"
#include "NetworkSessionCore.h"
#include "ToolKit/Scene.h"

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

      String QuoteArgument(const String& value)
      {
        return "\"" + value + "\"";
      }

      void AddFlag(std::ostringstream& builder, const String& flag)
      {
        if (!flag.empty())
        {
          builder << ' ' << flag;
        }
      }

      void AddOption(std::ostringstream& builder, const char* key, const String& value)
      {
        if (!value.empty())
        {
          builder << ' ' << key << '=' << QuoteArgument(value);
        }
      }

      void AddOption(std::ostringstream& builder, const char* key, uint16_t value)
      {
        if (value != 0)
        {
          builder << ' ' << key << '=' << value;
        }
      }

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
        if (!CopyConfigDirectory(session.configTemplateRoot, configRoot))
        {
          errorMessage = "Failed to prepare isolated child config files.";
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

#ifdef _WIN32
      bool LaunchChildRuntimeProcess(const String& executablePath,
                                     const String& manifestPath,
                                     const NetworkPlayInstanceSpec& instance,
                                     PluginMain::ChildProcessInfo& childProcess,
                                     String& errorMessage)
      {
        std::ostringstream commandLineBuilder;
        commandLineBuilder << QuoteArgument(executablePath);
        AddOption(commandLineBuilder, "-networkPlayManifest", manifestPath);
        AddFlag(commandLineBuilder, GetNetworkPlayRoleFlag(instance.hostingMode));
        if (instance.headless)
        {
          AddFlag(commandLineBuilder, "-headless");
        }
        AddOption(commandLineBuilder, "-connectHost", instance.connectHost);
        AddOption(commandLineBuilder, "-connectPort", instance.connectPort);
        AddOption(commandLineBuilder, "-listenPort", instance.listenPort);
        AddOption(commandLineBuilder, "-bindAddress", instance.bindAddress);
        AddOption(commandLineBuilder, "-advertisedAddress", instance.advertisedAddress);

        const std::wstring executable =
            PlatformHelpers::UTF8Util::ConvertUTF8ToUTF16(executablePath);
        const std::wstring commandLine =
            PlatformHelpers::UTF8Util::ConvertUTF8ToUTF16(commandLineBuilder.str());
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                                commandLine.end());
        mutableCommandLine.push_back(L'\0');

        STARTUPINFOW startupInfo {};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = instance.headless ? SW_HIDE : SW_SHOWNORMAL;

        PROCESS_INFORMATION processInfo {};
        const std::wstring workingDirectory =
            std::filesystem::path(executablePath).parent_path().wstring();
        if (!CreateProcessW(executable.c_str(),
                            mutableCommandLine.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            0,
                            nullptr,
                            workingDirectory.c_str(),
                            &startupInfo,
                            &processInfo))
        {
          errorMessage = "CreateProcessW failed for child runtime.";
          return false;
        }

        CloseHandle(processInfo.hThread);
        childProcess.processHandle =
            reinterpret_cast<uintptr_t>(processInfo.hProcess);
        childProcess.processId = processInfo.dwProcessId;
        childProcess.manifestPath = manifestPath;
        childProcess.roleName = instance.roleName;
        return true;
      }
#endif

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
          m_networkManager->Stop();
          m_networkManager = nullptr;
        }
        RestoreNetworkManagerOverrides();
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
			ToolKitNetworking::NetworkComponent::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkComponent:NetworkComponent";
			ToolKitNetworking::NetworkManager::StaticClass()->MetaKeys[ToolKit::Editor::ComponentMenuMetaKey] = "ToolKitNetworking/NetworkManager:NetworkManager";

			GetObjectFactory()->Register<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Register<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnUnload(XmlDocumentPtr state)
    {
      (void) state;
      TerminateTrackedChildProcesses(true);
      RestoreNetworkManagerOverrides();
			if (m_networkManager)
			{
				m_networkManager->Stop();
				m_networkManager = nullptr;
			}
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkComponent>();
			GetObjectFactory()->Unregister<ToolKitNetworking::NetworkManager>();
		}

		void PluginMain::OnPlay()
    {
      m_abortEditorPlayRequested = false;
      m_abortEditorPlayReason.clear();
      m_keepChildrenAliveAfterStop = false;

      const NetworkPlaySettings& settings = GetApp()->m_networkPlaySettings;
      NetworkPlayPlannerSettings plannerSettings;
      plannerSettings.playerCount = static_cast<uint>(settings.PlayerCount);
      plannerSettings.runDedicatedServerHeadless =
          settings.RunDedicatedServerHeadless;
      plannerSettings.basePort = settings.BasePort;
      plannerSettings.autoAllocatePorts = settings.AutoAllocatePorts;
      plannerSettings.topology = settings.Topology;
      if (settings.Enabled)
      {
        if (!StartNetworkPlaySession())
        {
          return;
        }
        return;
      }

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
				m_networkManager->Stop();
				m_networkManager = nullptr;
			}

      RestoreNetworkManagerOverrides();
      TerminateTrackedChildProcesses(!m_keepChildrenAliveAfterStop);
      m_keepChildrenAliveAfterStop = false;
		}

    void PluginMain::PollChildProcesses()
    {
#ifdef _WIN32
      for (auto it = m_childProcesses.begin(); it != m_childProcesses.end();)
      {
        HANDLE processHandle = reinterpret_cast<HANDLE>(it->processHandle);
        if (processHandle == nullptr)
        {
          it = m_childProcesses.erase(it);
          continue;
        }

        DWORD exitCode = 0;
        if (!GetExitCodeProcess(processHandle, &exitCode) ||
            exitCode != STILL_ACTIVE)
        {
          CloseHandle(processHandle);
          it = m_childProcesses.erase(it);
          continue;
        }

        ++it;
      }
#else
      m_childProcesses.clear();
#endif
    }

    void PluginMain::RequestAbortPlay(const String& reason)
    {
      TK_ERR("%s", reason.c_str());
      m_abortEditorPlayRequested = true;
      m_abortEditorPlayReason = reason;
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
#ifdef _WIN32
      for (ChildProcessInfo& child : m_childProcesses)
      {
        HANDLE processHandle = reinterpret_cast<HANDLE>(child.processHandle);
        if (processHandle == nullptr)
        {
          continue;
        }

        if (force)
        {
          DWORD exitCode = 0;
          if (GetExitCodeProcess(processHandle, &exitCode) && exitCode == STILL_ACTIVE)
          {
            TerminateProcess(processHandle, 1);
            WaitForSingleObject(processHandle, 2000);
          }
        }

        CloseHandle(processHandle);
      }
#endif
      m_childProcesses.clear();
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
      TerminateTrackedChildProcesses(true);
      RestoreNetworkManagerOverrides();

#ifndef _WIN32
      RequestAbortPlay("Editor network play currently supports Windows child launches only.");
      return false;
#else
      NetworkManagerPtrArray managers;
      if (!CollectSceneNetworkManagers(managers))
      {
        RequestAbortPlay("No active scene is available for editor network play.");
        return false;
      }
      if (managers.size() != 1)
      {
        RequestAbortPlay("Editor network play requires exactly one active NetworkManager in the scene.");
        return false;
      }

      m_networkManager = managers.front();
      if (!m_networkManager)
      {
        RequestAbortPlay("Editor network play could not acquire the scene NetworkManager.");
        return false;
      }

      const SessionBootstrapConfig sessionConfig =
          m_networkManager->GetSessionBootstrapConfig();
      if (sessionConfig.joinMethod != JoinMethod::DirectAddress)
      {
        RequestAbortPlay("Editor network play currently supports DirectAddress only.");
        return false;
      }

      const Project activeProject = GetApp()->m_workspace.GetActiveProject();
      if (activeProject.name.empty())
      {
        RequestAbortPlay("Editor network play requires an active project.");
        return false;
      }

      EditorScenePtr currentScene = Cast<EditorScene>(GetSceneManager()->GetCurrentScene());
      if (!currentScene)
      {
        RequestAbortPlay("Editor network play requires an active scene.");
        return false;
      }

      const String scenePath = currentScene->GetFile();
      if (scenePath.empty() || !CheckFile(scenePath))
      {
        RequestAbortPlay("Editor network play requires the current scene to be saved to disk.");
        return false;
      }

      const String workspaceRoot = GetApp()->m_workspace.GetActiveWorkspace();
      const String projectRoot =
          ConcatPaths({workspaceRoot, activeProject.name});
      const String executablePath =
          GetApp()->m_workspace.GetBinPath() + ".exe";
      if (!CheckFile(executablePath))
      {
        RequestAbortPlay("Editor network play could not find the standalone runtime executable.");
        return false;
      }

      const NetworkPlaySettings& settings = GetApp()->m_networkPlaySettings;
      NetworkPlayPlannerSettings plannerSettings;
      plannerSettings.playerCount = static_cast<uint>(settings.PlayerCount);
      plannerSettings.runDedicatedServerHeadless =
          settings.RunDedicatedServerHeadless;
      plannerSettings.basePort = settings.BasePort;
      plannerSettings.autoAllocatePorts = settings.AutoAllocatePorts;
      plannerSettings.topology = settings.Topology;
      NetworkPlaySessionSpec session;
      NetworkPlaySceneConfig sceneConfig;
      sceneConfig.joinMethod = sessionConfig.joinMethod;
      sceneConfig.maxClients = m_networkManager->GetMaxClientsVal();
      sceneConfig.connectHost = m_networkManager->GetConnectHostVal();
      sceneConfig.connectPort = static_cast<uint16_t>(m_networkManager->GetConnectPortVal());
      sceneConfig.listenPort = static_cast<uint16_t>(m_networkManager->GetListenPortVal());
      sceneConfig.bindAddress = m_networkManager->GetBindAddressVal();
      sceneConfig.advertisedAddress = m_networkManager->GetAdvertisedAddressVal();

      NetworkPlaySessionMetadata metadata;
      metadata.launchId = BuildNetworkPlayLaunchId();
      metadata.launchRoot =
          ConcatPaths({projectRoot, "Intermediate", "NetworkPlay", metadata.launchId});
      metadata.projectRoot = projectRoot;
      metadata.workspaceRoot = workspaceRoot;
      metadata.configTemplateRoot = GetApp()->m_workspace.GetConfigDirectory();
      metadata.resourceRoot = GetApp()->m_workspace.GetResourceRoot();
      metadata.scenePath = scenePath;
      metadata.executablePath = executablePath;

      String planningError;
      if (!BuildNetworkPlaySessionSpec(plannerSettings, sceneConfig, metadata, session,
                                       planningError))
      {
        RequestAbortPlay(planningError.empty()
                             ? "Failed to build the editor network play session."
                             : planningError);
        return false;
      }

      m_keepChildrenAliveAfterStop = !settings.AutoStopChildren;

      if (settings.Topology == NetworkPlayTopology::DedicatedServer &&
          !session.childInstances.empty())
      {
        String errorMessage;
        const NetworkPlayInstanceSpec& serverChild = session.childInstances.front();
        const String manifestPath = ConcatPaths(
            {session.launchRoot, "instances", serverChild.instanceId + ".settings"});

        ChildProcessInfo childProcess;
        if (!WriteNetworkPlayManifest(manifestPath, session, serverChild, errorMessage) ||
            !LaunchChildRuntimeProcess(executablePath,
                                       manifestPath,
                                       serverChild,
                                       childProcess,
                                       errorMessage))
        {
          RequestAbortPlay(errorMessage.empty()
                               ? "Failed to start the dedicated server child process."
                               : errorMessage);
          TerminateTrackedChildProcesses(true);
          m_networkManager = nullptr;
          return false;
        }

        m_childProcesses.push_back(childProcess);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }

      ConfigureLocalNetworkManager(*m_networkManager, m_overrideState, session.localInstance);
      if (!m_networkManager->StartConfiguredSession())
      {
        TerminateTrackedChildProcesses(true);
        RestoreNetworkManagerOverrides();
        RequestAbortPlay("Failed to start the local NetworkManager for editor network play.");
        m_networkManager = nullptr;
        return false;
      }

      if (settings.Topology == NetworkPlayTopology::ListenServer)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }

      const size_t childStartIndex =
          settings.Topology == NetworkPlayTopology::DedicatedServer ? 1u : 0u;
      for (size_t childIndex = childStartIndex; childIndex < session.childInstances.size();
           ++childIndex)
      {
        const NetworkPlayInstanceSpec& childSpec = session.childInstances[childIndex];
        const String manifestPath = ConcatPaths(
            {session.launchRoot, "instances", childSpec.instanceId + ".settings"});
        String errorMessage;
        ChildProcessInfo childProcess;
        if (!WriteNetworkPlayManifest(manifestPath, session, childSpec, errorMessage) ||
            !LaunchChildRuntimeProcess(executablePath,
                                       manifestPath,
                                       childSpec,
                                       childProcess,
                                       errorMessage))
        {
          m_networkManager->Stop();
          TerminateTrackedChildProcesses(true);
          RestoreNetworkManagerOverrides();
          RequestAbortPlay(errorMessage.empty()
                               ? "Failed to start one of the editor network-play child processes."
                               : errorMessage);
          m_networkManager = nullptr;
          return false;
        }

        m_childProcesses.push_back(childProcess);
      }

      TK_LOG("Editor network play launched.");
      return true;
#endif
    }

	} // namespace Editor
} // namespace ToolKit
