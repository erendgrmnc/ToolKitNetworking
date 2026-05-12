#include <Util.h>

namespace ToolKit {
class Logger;
Logger* GetLogger();
}

#include "PluginMain.h"

#include <gtest/gtest.h>
#include <algorithm>

namespace ToolKit::Editor {
namespace {

using ToolKitNetworking::HostingMode;
using ToolKitNetworking::JoinMethod;
using ToolKitNetworking::NetworkManager;
using ToolKitNetworking::NetworkRole;

class TestNetworkManager final : public NetworkManager {
public:
  TestNetworkManager() { NativeConstruct(true); }

  void SetRole(NetworkRole role) {
    MultiChoiceVariant value = GetRoleVal();
    value.SetEnum(role);
    SetRoleVal(value);
  }
};

class TestPluginMain final : public PluginMain {
public:
  bool enabled = true;
  bool resolveContextResult = true;
  bool startLocalResult = true;
  int failManifestOnCall = -1;
  int failLaunchOnCall = -1;
  int manifestCalls = 0;
  int launchCalls = 0;
  int startLocalCalls = 0;
  int stopLocalCalls = 0;
  int releaseCalls = 0;
  int forceReleaseCalls = 0;
  std::vector<String> manifestXmls;
  std::vector<String> launchedRoles;
  std::vector<bool> launchedHeadless;
  std::vector<uintptr_t> activeHandles;
  NetworkPlayStartContext context;

  bool IsEditorNetworkPlayEnabled() const override { return enabled; }

  bool ResolveNetworkPlayStartContext(NetworkPlayStartContext& resolved,
                                      String& errorMessage) override {
    if (!resolveContextResult) {
      errorMessage = "Injected context failure.";
      return false;
    }

    resolved = context;
    return true;
  }

  bool WriteChildManifestFile(const String& manifestPath,
                              const NetworkPlaySessionSpec& session,
                              const NetworkPlayInstanceSpec& instance,
                              String& errorMessage) override {
    ++manifestCalls;
    if (failManifestOnCall == manifestCalls) {
      errorMessage = "Injected manifest write failure.";
      return false;
    }

    manifestXmls.push_back(BuildNetworkPlayManifestXml(
        session, instance, ConcatPaths({session.launchRoot, "Config", instance.instanceId})));
    lastManifestPath = manifestPath;
    return true;
  }

  bool LaunchChildProcess(const String& executablePath,
                          const String& manifestPath,
                          const NetworkPlayInstanceSpec& instance,
                          ChildProcessInfo& childProcess,
                          String& errorMessage) override {
    (void) executablePath;
    (void) manifestPath;
    ++launchCalls;
    if (failLaunchOnCall == launchCalls) {
      errorMessage = "Injected child launch failure.";
      return false;
    }

    childProcess.process.processHandle = static_cast<uintptr_t>(1000 + launchCalls);
    childProcess.process.processId = static_cast<unsigned long>(2000 + launchCalls);
    childProcess.manifestPath = manifestPath;
    childProcess.roleName = instance.roleName;
    activeHandles.push_back(childProcess.process.processHandle);
    launchedRoles.push_back(instance.roleName);
    launchedHeadless.push_back(instance.headless);
    return true;
  }

  bool IsChildProcessActive(const ChildProcessInfo& child) const override {
    return std::find(activeHandles.begin(), activeHandles.end(),
                     child.process.processHandle) != activeHandles.end();
  }

  void ReleaseChildProcess(ChildProcessInfo& child, bool forceTerminate) override {
    ++releaseCalls;
    if (forceTerminate) {
      ++forceReleaseCalls;
    }

    activeHandles.erase(std::remove(activeHandles.begin(),
                                    activeHandles.end(),
                                    child.process.processHandle),
                        activeHandles.end());
    child.process.processHandle = 0;
    child.process.processId = 0;
  }

  void SleepForChildStartup(uint) override {}

  bool StartLocalConfiguredSession() override {
    ++startLocalCalls;
    return startLocalResult;
  }

  void StopLocalConfiguredSession() override { ++stopLocalCalls; }

  size_t GetTrackedChildCount() const { return m_childProcesses.size(); }
  bool HasAbortRequest() const { return m_abortEditorPlayRequested; }
  String GetAbortReason() const { return m_abortEditorPlayReason; }
  bool OverrideActive() const { return m_overrideState.active; }
  ToolKitNetworking::NetworkManagerPtr GetManager() const { return m_networkManager; }
  bool KeepChildrenAliveAfterStop() const { return m_keepChildrenAliveAfterStop; }

  void TriggerDeferredAbort(const String& reason) { RequestAbortPlay(reason); }

  String lastManifestPath;
};

PluginMain::NetworkPlayStartContext MakeContext(TestNetworkManager& manager,
                                                NetworkPlayTopology topology,
                                                uint playerCount) {
  PluginMain::NetworkPlayStartContext context;
  context.networkManager = std::shared_ptr<NetworkManager>(&manager, [](NetworkManager*) {});
  context.autoStopChildren = true;
  context.plannerSettings.playerCount = playerCount;
  context.plannerSettings.autoAllocatePorts = false;
  context.plannerSettings.basePort = 7777;
  context.plannerSettings.runDedicatedServerHeadless = true;
  context.plannerSettings.topology = topology;
  context.sceneConfig.joinMethod = JoinMethod::DirectAddress;
  context.sceneConfig.maxClients = 4;
  context.sceneConfig.connectHost = "198.51.100.25";
  context.sceneConfig.connectPort = 9555;
  context.sceneConfig.listenPort = 7777;
  context.sceneConfig.bindAddress = "0.0.0.0";
  context.sceneConfig.advertisedAddress = "198.51.100.25";
  context.metadata.launchId = "launch-1";
  context.metadata.launchRoot = "C:/Project/Intermediate/NetworkPlay/launch-1";
  context.metadata.projectRoot = "C:/Project";
  context.metadata.workspaceRoot = "C:/Workspace";
  context.metadata.configTemplateRoot = "C:/Project/Config";
  context.metadata.resourceRoot = "C:/Project/Resources";
  context.metadata.scenePath = "C:/Project/Resources/Scenes/Test.scene";
  context.metadata.executablePath = "C:/Project/Bin/Game.exe";
  context.metadata.runtimePluginNames = {"RuntimeGameplay"};
  return context;
}

TEST(EditorNetworkPlayPluginTests,
     ListenServerTwoPlayersLaunchesOneChildAndRestoresOverridesOnStop) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Client);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::ListenServer, 2);

  plugin.OnPlay();

  ASSERT_EQ(plugin.startLocalCalls, 1);
  ASSERT_EQ(plugin.GetTrackedChildCount(), 1u);
  ASSERT_EQ(plugin.launchCalls, 1);
  ASSERT_EQ(plugin.manifestCalls, 1);
  EXPECT_EQ(plugin.launchedRoles.front(), "Client");
  EXPECT_FALSE(plugin.launchedHeadless.front());
  EXPECT_NE(plugin.manifestXmls.front().find("sceneSnapshotPath=\"C:/Project/Intermediate/NetworkPlay/launch-1/Scenes/Current.scene\""), String::npos);
  EXPECT_NE(plugin.manifestXmls.front().find("<Plugin name=\"RuntimeGameplay\" />"), String::npos);
  EXPECT_EQ(manager.GetRoleVal().GetEnum<NetworkRole>(), NetworkRole::Host);
  EXPECT_TRUE(plugin.OverrideActive());

  plugin.OnStop();

  EXPECT_EQ(plugin.stopLocalCalls, 1);
  EXPECT_EQ(plugin.GetTrackedChildCount(), 0u);
  EXPECT_EQ(plugin.forceReleaseCalls, 1);
  EXPECT_EQ(manager.GetRoleVal().GetEnum<NetworkRole>(), NetworkRole::Client);
  EXPECT_FALSE(plugin.OverrideActive());
}

TEST(EditorNetworkPlayPluginTests,
     DedicatedServerThreePlayersLaunchesServerAndTwoClientChildren) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Client);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::DedicatedServer, 3);

  plugin.OnPlay();

  ASSERT_EQ(plugin.startLocalCalls, 1);
  ASSERT_EQ(plugin.GetTrackedChildCount(), 3u);
  ASSERT_EQ(plugin.launchCalls, 3);
  ASSERT_EQ(plugin.manifestCalls, 3);
  EXPECT_EQ(plugin.launchedRoles[0], "DedicatedServer");
  EXPECT_TRUE(plugin.launchedHeadless[0]);
  EXPECT_EQ(plugin.launchedRoles[1], "Client");
  EXPECT_EQ(plugin.launchedRoles[2], "Client");
  EXPECT_EQ(manager.GetRoleVal().GetEnum<NetworkRole>(), NetworkRole::Client);
  EXPECT_EQ(manager.GetConnectHostVal(), "127.0.0.1");
  EXPECT_EQ(manager.GetConnectPortVal(), 7777u);
}

TEST(EditorNetworkPlayPluginTests,
     ClientAttachTwoPlayersUsesRemoteTargetAndOneChild) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Host);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::ClientAttach, 2);

  plugin.OnPlay();

  ASSERT_EQ(plugin.startLocalCalls, 1);
  ASSERT_EQ(plugin.GetTrackedChildCount(), 1u);
  EXPECT_EQ(plugin.launchCalls, 1);
  EXPECT_EQ(manager.GetRoleVal().GetEnum<NetworkRole>(), NetworkRole::Client);
  EXPECT_EQ(manager.GetConnectHostVal(), "198.51.100.25");
  EXPECT_EQ(manager.GetConnectPortVal(), 9555u);
}

TEST(EditorNetworkPlayPluginTests,
     PartialLaunchFailureCleansUpChildrenAndRequestsAbort) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Client);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::DedicatedServer, 3);
  plugin.failLaunchOnCall = 2;

  plugin.OnPlay();

  EXPECT_TRUE(plugin.HasAbortRequest());
  EXPECT_NE(plugin.GetAbortReason().find("Injected child launch failure"),
            String::npos);
  EXPECT_EQ(plugin.GetTrackedChildCount(), 0u);
  EXPECT_EQ(plugin.stopLocalCalls, 1);
  EXPECT_FALSE(plugin.OverrideActive());
}

TEST(EditorNetworkPlayPluginTests,
     DeferredAbortFrameStopsSessionAndCleansUpTrackedChildren) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Client);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::ListenServer, 2);
  plugin.OnPlay();
  ASSERT_EQ(plugin.GetTrackedChildCount(), 1u);

  plugin.TriggerDeferredAbort("Injected deferred abort.");
  plugin.Frame(0.0f);

  EXPECT_FALSE(plugin.HasAbortRequest());
  EXPECT_EQ(plugin.stopLocalCalls, 1);
  EXPECT_EQ(plugin.GetTrackedChildCount(), 0u);
  EXPECT_FALSE(plugin.OverrideActive());
}

TEST(EditorNetworkPlayPluginTests, OnUnloadAlsoStopsAndTearsDownChildren) {
  TestNetworkManager manager;
  manager.SetRole(NetworkRole::Client);

  TestPluginMain plugin;
  plugin.context = MakeContext(manager, NetworkPlayTopology::ListenServer, 2);
  plugin.OnPlay();
  ASSERT_EQ(plugin.GetTrackedChildCount(), 1u);

  plugin.OnUnload(nullptr);

  EXPECT_EQ(plugin.stopLocalCalls, 1);
  EXPECT_EQ(plugin.GetTrackedChildCount(), 0u);
  EXPECT_EQ(plugin.forceReleaseCalls, 1);
}

} // namespace
} // namespace ToolKit::Editor
