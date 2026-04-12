#include "EditorNetworkPlayPlanner.h"

#include <gtest/gtest.h>

namespace ToolKit::Editor {
namespace {

NetworkPlaySessionMetadata MakeMetadata() {
  NetworkPlaySessionMetadata metadata;
  metadata.launchId = "launch-1";
  metadata.launchRoot = "C:/Project/Intermediate/NetworkPlay/launch-1";
  metadata.projectRoot = "C:/Project";
  metadata.workspaceRoot = "C:/Workspace";
  metadata.configTemplateRoot = "C:/Project/Config";
  metadata.resourceRoot = "C:/Project/Resources";
  metadata.scenePath = "C:/Project/Resources/Scenes/Test.scene";
  metadata.executablePath = "C:/Project/Bin/Game.exe";
  return metadata;
}

NetworkPlaySceneConfig MakeDirectSceneConfig() {
  NetworkPlaySceneConfig scene;
  scene.joinMethod = ToolKitNetworking::JoinMethod::DirectAddress;
  scene.maxClients = 4;
  scene.connectHost = "203.0.113.10";
  scene.connectPort = 9000;
  scene.listenPort = 7777;
  scene.bindAddress = "0.0.0.0";
  scene.advertisedAddress = "203.0.113.10";
  return scene;
}

} // namespace

TEST(EditorNetworkPlayPlannerTest,
     ListenServerTwoPlayersUsesLocalHostAndOneClientChild) {
  NetworkPlayPlannerSettings settings;
  settings.topology = NetworkPlayTopology::ListenServer;
  settings.playerCount = 2;
  settings.autoAllocatePorts = false;
  settings.basePort = 7777;

  NetworkPlaySessionSpec session;
  String error;
  ASSERT_TRUE(BuildNetworkPlaySessionSpec(settings, MakeDirectSceneConfig(),
                                          MakeMetadata(), session, error))
      << error;

  EXPECT_EQ(session.playerCount, 2u);
  EXPECT_EQ(session.basePort, 7777);
  EXPECT_EQ(session.localInstance.hostingMode,
            ToolKitNetworking::HostingMode::ListenServer);
  EXPECT_EQ(session.localInstance.connectHost, "127.0.0.1");
  EXPECT_EQ(session.localInstance.connectPort, 7777);
  ASSERT_EQ(session.childInstances.size(), 1u);
  EXPECT_EQ(session.childInstances.front().hostingMode,
            ToolKitNetworking::HostingMode::Client);
  EXPECT_EQ(session.childInstances.front().connectHost, "127.0.0.1");
  EXPECT_EQ(session.childInstances.front().connectPort, 7777);
}

TEST(EditorNetworkPlayPlannerTest,
     DedicatedServerThreePlayersUsesHeadlessServerAndTwoClients) {
  NetworkPlayPlannerSettings settings;
  settings.topology = NetworkPlayTopology::DedicatedServer;
  settings.playerCount = 3;
  settings.runDedicatedServerHeadless = true;
  settings.autoAllocatePorts = false;
  settings.basePort = 8000;

  NetworkPlaySessionSpec session;
  String error;
  ASSERT_TRUE(BuildNetworkPlaySessionSpec(settings, MakeDirectSceneConfig(),
                                          MakeMetadata(), session, error))
      << error;

  EXPECT_EQ(session.localInstance.hostingMode,
            ToolKitNetworking::HostingMode::Client);
  EXPECT_EQ(session.localInstance.connectHost, "127.0.0.1");
  EXPECT_EQ(session.localInstance.connectPort, 8000);
  ASSERT_EQ(session.childInstances.size(), 3u);
  EXPECT_EQ(session.childInstances.front().hostingMode,
            ToolKitNetworking::HostingMode::DedicatedServer);
  EXPECT_TRUE(session.childInstances.front().headless);
  EXPECT_EQ(session.childInstances.front().listenPort, 8000);
  EXPECT_EQ(session.childInstances[1].hostingMode,
            ToolKitNetworking::HostingMode::Client);
  EXPECT_EQ(session.childInstances[2].hostingMode,
            ToolKitNetworking::HostingMode::Client);
  EXPECT_EQ(session.childInstances[1].connectPort, 8000);
  EXPECT_EQ(session.childInstances[2].connectPort, 8000);
}

TEST(EditorNetworkPlayPlannerTest,
     DedicatedServerRejectsPlayerCountsAboveSceneMaxClients) {
  NetworkPlayPlannerSettings settings;
  settings.topology = NetworkPlayTopology::DedicatedServer;
  settings.playerCount = 5;

  NetworkPlaySceneConfig scene = MakeDirectSceneConfig();
  scene.maxClients = 2;

  NetworkPlaySessionSpec session;
  String error;
  EXPECT_FALSE(BuildNetworkPlaySessionSpec(settings, scene, MakeMetadata(),
                                           session, error));
  EXPECT_NE(error.find("MaxClients"), String::npos);
}

TEST(EditorNetworkPlayPlannerTest, ClientAttachRequiresExplicitConnectTarget) {
  NetworkPlayPlannerSettings settings;
  settings.topology = NetworkPlayTopology::ClientAttach;
  settings.playerCount = 2;

  NetworkPlaySceneConfig scene = MakeDirectSceneConfig();
  scene.connectHost.clear();
  scene.connectPort = 0;

  NetworkPlaySessionSpec session;
  String error;
  EXPECT_FALSE(BuildNetworkPlaySessionSpec(settings, scene, MakeMetadata(),
                                           session, error));
  EXPECT_NE(error.find("ConnectHost"), String::npos);
}

TEST(EditorNetworkPlayPlannerTest,
     ManifestXmlContainsEscapedPathsAndRoleSpecificFields) {
  NetworkPlayPlannerSettings settings;
  settings.topology = NetworkPlayTopology::ListenServer;
  settings.playerCount = 2;
  settings.autoAllocatePorts = false;
  settings.basePort = 7777;

  NetworkPlaySessionMetadata metadata = MakeMetadata();
  metadata.scenePath = "C:/Project/Resources/Scenes/Test & Validate.scene";

  NetworkPlaySessionSpec session;
  String error;
  ASSERT_TRUE(BuildNetworkPlaySessionSpec(settings, MakeDirectSceneConfig(),
                                          metadata, session, error))
      << error;

  const String xml = BuildNetworkPlayManifestXml(
      session, session.childInstances.front(),
      "C:/Project/Intermediate/NetworkPlay/launch-1/Config/client-2");

  EXPECT_NE(xml.find("instanceId=\"client-2\""), String::npos);
  EXPECT_NE(xml.find("topology=\"ListenServer\""), String::npos);
  EXPECT_NE(xml.find("joinMethod=\"DirectAddress\""), String::npos);
  EXPECT_NE(xml.find("scenePath=\"C:/Project/Resources/Scenes/Test &amp; Validate.scene\""),
            String::npos);
  EXPECT_NE(xml.find("connectHost=\"127.0.0.1\""), String::npos);
  EXPECT_NE(xml.find("connectPort=\"7777\""), String::npos);
}

} // namespace ToolKit::Editor
