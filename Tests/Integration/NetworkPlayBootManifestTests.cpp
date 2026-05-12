#include "NetworkPlayBootManifest.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace ToolKit {
namespace {

TEST(NetworkPlayBootManifestTests, FindsManifestPathArgument) {
  char arg0[] = "Game.exe";
  char arg1[] = "-networkPlayManifest=C:/Temp/launch/settings.xml";
  char* argv[] = {arg0, arg1};

  EXPECT_EQ(FindNetworkPlayManifestPath(2, argv),
            "C:/Temp/launch/settings.xml");
}

TEST(NetworkPlayBootManifestTests, ParseXmlReadsExplicitFields) {
  const String xml =
      "<NetworkPlayInstance configRoot=\"C:/Temp/Config\" "
      "resourceRoot=\"C:/Temp/Resources\" "
      "projectRoot=\"C:/Project\" "
      "workspaceRoot=\"C:/Workspace\" "
      "scenePath=\"C:/Project/Resources/Scenes/Test.scene\" "
      "sceneSnapshotPath=\"C:/Project/Intermediate/NetworkPlay/Scene.scene\" "
      "tempRoot=\"C:/Temp/Work\" logRoot=\"C:/Temp/Logs\" "
      "headless=\"true\" autoPlay=\"1\"><RuntimePlugins><Plugin name=\"RuntimeGameplay\" /></RuntimePlugins></NetworkPlayInstance>";

  NetworkPlayBootManifest manifest;
  ASSERT_TRUE(ParseNetworkPlayManifestXml(xml, manifest));
  EXPECT_TRUE(manifest.Valid);
  EXPECT_TRUE(manifest.Headless);
  EXPECT_TRUE(manifest.AutoPlay);
  EXPECT_EQ(manifest.ConfigRoot, "C:/Temp/Config");
  EXPECT_EQ(manifest.ResourceRoot, "C:/Temp/Resources");
  EXPECT_EQ(manifest.ProjectRoot, "C:/Project");
  EXPECT_EQ(manifest.WorkspaceRoot, "C:/Workspace");
  EXPECT_EQ(manifest.ScenePath, "C:/Project/Resources/Scenes/Test.scene");
  EXPECT_EQ(manifest.SceneSnapshotPath, "C:/Project/Intermediate/NetworkPlay/Scene.scene");
  EXPECT_EQ(manifest.TempRoot, "C:/Temp/Work");
  EXPECT_EQ(manifest.LogRoot, "C:/Temp/Logs");
  ASSERT_EQ(manifest.RuntimePlugins.size(), 1u);
  EXPECT_EQ(manifest.RuntimePlugins.front(), "RuntimeGameplay");
}

TEST(NetworkPlayBootManifestTests, ParseXmlDerivesRootsFromProjectRoot) {
  const String xml =
      "<NetworkPlayManifest projectRoot=\"C:/Project\" "
      "scenePath=\"C:/Project/Resources/Scenes/Test.scene\" />";

  NetworkPlayBootManifest manifest;
  ASSERT_TRUE(ParseNetworkPlayManifestXml(xml, manifest));
  EXPECT_TRUE(manifest.Valid);
  EXPECT_EQ(manifest.ProjectRoot, "C:/Project");
  EXPECT_EQ(manifest.ConfigRoot, "C:/Project/Config");
  EXPECT_EQ(manifest.ResourceRoot, "C:/Project/Resources");
}

TEST(NetworkPlayBootManifestTests, ParseXmlRejectsMalformedPayload) {
  const String xml =
      "<NetworkPlayInstance projectRoot=\"C:/Project\" scenePath=\"broken\">";

  NetworkPlayBootManifest manifest;
  EXPECT_FALSE(ParseNetworkPlayManifestXml(xml, manifest));
  EXPECT_FALSE(manifest.Valid);
}

TEST(NetworkPlayBootManifestTests, ParseFileReadsManifestFromDisk) {
  const std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "toolkit_network_play_tests";
  std::filesystem::create_directories(tempDir);
  const std::filesystem::path manifestPath = tempDir / "boot.settings";

  {
    std::ofstream manifestFile(manifestPath);
    manifestFile << "<NetworkPlayInstance projectRoot=\"C:/Project\" "
                    "scenePath=\"C:/Project/Resources/Scenes/Test.scene\" "
                    "autoPlay=\"true\" />";
  }

  NetworkPlayBootManifest manifest;
  ASSERT_TRUE(ParseNetworkPlayManifestFile(manifestPath.string(), manifest));
  EXPECT_TRUE(manifest.Valid);
  EXPECT_TRUE(manifest.AutoPlay);
  EXPECT_EQ(manifest.ConfigRoot, "C:/Project/Config");
  EXPECT_EQ(manifest.ResourceRoot, "C:/Project/Resources");

  std::error_code ec;
  std::filesystem::remove(manifestPath, ec);
  std::filesystem::remove(tempDir, ec);
}

} // namespace
} // namespace ToolKit
