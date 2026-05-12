#include "NetworkPlayBootRuntime.h"

#include <gtest/gtest.h>

namespace ToolKit {
namespace {

TEST(NetworkPlayBootRuntimeTests, HeadlessModeHonorsCliOrManifestRequest) {
  NetworkPlayBootManifest manifest;
  manifest.Valid = true;
  manifest.Headless = true;

  EXPECT_TRUE(ResolveNetworkPlayHeadlessMode(false, manifest));
  EXPECT_TRUE(ResolveNetworkPlayHeadlessMode(true, manifest));

  manifest.Headless = false;
  EXPECT_FALSE(ResolveNetworkPlayHeadlessMode(false, manifest));
  EXPECT_TRUE(ResolveNetworkPlayHeadlessMode(true, manifest));
}

TEST(NetworkPlayBootRuntimeTests, AutoPlayDefaultsToTrueWithoutManifest) {
  NetworkPlayBootManifest manifest;
  EXPECT_TRUE(ShouldAutoPlayNetworkPlayBoot(manifest));
}

TEST(NetworkPlayBootRuntimeTests, AutoPlayUsesManifestFlagWhenPresent) {
  NetworkPlayBootManifest manifest;
  manifest.Valid = true;
  manifest.AutoPlay = true;
  EXPECT_TRUE(ShouldAutoPlayNetworkPlayBoot(manifest));

  manifest.AutoPlay = false;
  EXPECT_FALSE(ShouldAutoPlayNetworkPlayBoot(manifest));
}

TEST(NetworkPlayBootRuntimeTests, SceneSnapshotOverridesSavedScenePath) {
  NetworkPlayBootManifest manifest;
  manifest.Valid = true;
  manifest.ScenePath = "C:/Project/Resources/Scenes/Saved.scene";
  manifest.SceneSnapshotPath =
      "C:/Project/Intermediate/NetworkPlay/Current.scene";

  EXPECT_EQ(ResolveNetworkPlayScenePath(manifest),
            "C:/Project/Intermediate/NetworkPlay/Current.scene");

  manifest.SceneSnapshotPath.clear();
  EXPECT_EQ(ResolveNetworkPlayScenePath(manifest),
            "C:/Project/Resources/Scenes/Saved.scene");
}

} // namespace
} // namespace ToolKit
