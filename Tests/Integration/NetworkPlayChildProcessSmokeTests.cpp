#include <gtest/gtest.h>

#ifdef _WIN32
  #include <Windows.h>

  #include <chrono>
  #include <filesystem>
  #include <fstream>
  #include <string>
  #include <thread>
  #include <vector>
#endif

namespace {

#ifndef TK_NET_PROJECT_ROOT_DIR
  #define TK_NET_PROJECT_ROOT_DIR ""
#endif

#ifndef TK_NET_TESTS_OUTPUT_DIR
  #define TK_NET_TESTS_OUTPUT_DIR ""
#endif

#ifdef _WIN32

struct RuntimeFixturePaths {
  std::filesystem::path projectRoot;
  std::filesystem::path runtimeExe;
  std::filesystem::path scenePath;
  std::filesystem::path testRoot;
  std::filesystem::path configRoot;
  std::filesystem::path sceneSnapshotPath;
  std::filesystem::path manifestPath;
};

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int size = MultiByteToWideChar(CP_UTF8,
                                       0,
                                       value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr,
                                       0);
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      0,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }

  const int size = WideCharToMultiByte(CP_UTF8,
                                       0,
                                       value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr,
                                       0,
                                       nullptr,
                                       nullptr);
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8,
                      0,
                      value.data(),
                      static_cast<int>(value.size()),
                      result.data(),
                      size,
                      nullptr,
                      nullptr);
  return result;
}

std::string GenericPath(const std::filesystem::path& path) {
  return path.generic_string();
}

RuntimeFixturePaths BuildFixturePaths(const char* instanceId) {
  RuntimeFixturePaths paths;
  paths.projectRoot = std::filesystem::path(TK_NET_PROJECT_ROOT_DIR);
  paths.runtimeExe = paths.projectRoot / "Codes" / "Bin" / "eren.exe";
  paths.scenePath = paths.projectRoot / "Resources" / "Scenes" / "NetworkTest.scene";
  paths.testRoot = std::filesystem::path(TK_NET_TESTS_OUTPUT_DIR) /
                   "NetworkPlayChildProcessSmoke" / instanceId;
  paths.configRoot = paths.testRoot / "Config";
  paths.sceneSnapshotPath = paths.testRoot / "Scenes" / "Current.scene";
  paths.manifestPath = paths.testRoot / (std::string(instanceId) + ".settings");
  return paths;
}

void PrepareFixtureFiles(const RuntimeFixturePaths& paths) {
  std::error_code ec;
  std::filesystem::remove_all(paths.testRoot, ec);
  std::filesystem::create_directories(paths.configRoot);
  std::filesystem::create_directories(paths.sceneSnapshotPath.parent_path());
  std::filesystem::copy(paths.projectRoot / "Config",
                        paths.configRoot,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(paths.scenePath,
                             paths.sceneSnapshotPath,
                             std::filesystem::copy_options::overwrite_existing);
}

void WriteManifest(const RuntimeFixturePaths& paths,
                   const char* instanceId,
                   bool headless,
                   const char* role = "Client",
                   int connectPort = 7777,
                   int listenPort = 0) {
  std::ofstream manifest(paths.manifestPath, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(manifest.is_open()) << "Failed to create " << paths.manifestPath;

  manifest << "<NetworkPlayInstance"
           << " launchId=\"smoke-test\""
           << " instanceId=\"" << instanceId << "\""
           << " topology=\"ListenServer\""
           << " role=\"" << role << "\""
           << " projectRoot=\"" << GenericPath(paths.projectRoot) << "\""
           << " workspaceRoot=\"" << GenericPath(paths.projectRoot.parent_path()) << "\""
           << " scenePath=\"" << GenericPath(paths.scenePath) << "\""
           << " sceneSnapshotPath=\"" << GenericPath(paths.sceneSnapshotPath) << "\""
           << " configRoot=\"" << GenericPath(paths.configRoot) << "\""
           << " resourceRoot=\"" << GenericPath(paths.projectRoot / "Resources") << "\""
           << " tempRoot=\"" << GenericPath(paths.testRoot / "Temp") << "\""
           << " logRoot=\"" << GenericPath(paths.testRoot / "Logs") << "\""
           << " autoPlay=\"1\""
           << " headless=\"" << (headless ? "1" : "0") << "\""
           << " joinMethod=\"DirectAddress\""
           << " connectHost=\"127.0.0.1\""
           << " connectPort=\"" << connectPort << "\""
           << " listenPort=\"" << listenPort << "\""
           << " bindAddress=\"\""
           << " advertisedAddress=\"\""
           << " maxClients=\"3\">"
           << "<RuntimePlugins></RuntimePlugins>"
           << "</NetworkPlayInstance>";
}

struct ManagedChildProcess {
  HANDLE processHandle = nullptr;
  DWORD processId = 0;

  ~ManagedChildProcess() {
    if (processHandle == nullptr) {
      return;
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle, &exitCode) &&
        exitCode == STILL_ACTIVE) {
      TerminateProcess(processHandle, 1);
      WaitForSingleObject(processHandle, 2000);
    }

    CloseHandle(processHandle);
  }
};

DWORD GetProcessExitCode(const ManagedChildProcess& child) {
  DWORD exitCode = 0;
  GetExitCodeProcess(child.processHandle, &exitCode);
  return exitCode;
}

bool ProcessExited(const ManagedChildProcess& child) {
  return GetProcessExitCode(child) != STILL_ACTIVE;
}

struct WindowSearch {
  DWORD processId = 0;
  HWND runtimeWindow = nullptr;
  HWND errorWindow = nullptr;
  std::wstring errorTitle;
};

std::wstring GetWindowTitle(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  std::wstring title(static_cast<size_t>(length) + 1u, L'\0');
  if (length > 0) {
    GetWindowTextW(hwnd, title.data(), length + 1);
  }
  title.resize(static_cast<size_t>(length));
  return title;
}

bool TitleLooksLikeFatalRuntimeDialog(const std::wstring& title) {
  return title.find(L"System Error") != std::wstring::npos ||
         title.find(L"Assertion") != std::wstring::npos ||
         title.find(L"Microsoft Visual C++ Runtime Library") != std::wstring::npos;
}

BOOL CALLBACK FindProcessWindowsProc(HWND hwnd, LPARAM lparam) {
  WindowSearch* search = reinterpret_cast<WindowSearch*>(lparam);

  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != search->processId || !IsWindowVisible(hwnd)) {
    return TRUE;
  }

  const std::wstring title = GetWindowTitle(hwnd);
  wchar_t className[64] = {};
  GetClassNameW(hwnd, className, 64);

  if (TitleLooksLikeFatalRuntimeDialog(title) ||
      std::wstring(className) == L"#32770") {
    search->errorWindow = hwnd;
    search->errorTitle = title;
    return FALSE;
  }

  if (search->runtimeWindow == nullptr && title == L"ToolKit") {
    search->runtimeWindow = hwnd;
  }

  return TRUE;
}

WindowSearch FindProcessWindows(const ManagedChildProcess& child) {
  WindowSearch search;
  search.processId = child.processId;
  EnumWindows(FindProcessWindowsProc, reinterpret_cast<LPARAM>(&search));
  return search;
}

bool CaptureClientAreaHasNonBlackPixels(HWND hwnd) {
  RECT clientRect {};
  if (!GetClientRect(hwnd, &clientRect)) {
    return false;
  }

  POINT topLeft {clientRect.left, clientRect.top};
  ClientToScreen(hwnd, &topLeft);

  const int width = clientRect.right - clientRect.left;
  const int height = clientRect.bottom - clientRect.top;
  if (width <= 8 || height <= 8) {
    return false;
  }

  HDC screenDc = GetDC(nullptr);
  HDC memoryDc = CreateCompatibleDC(screenDc);
  HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
  HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

  const BOOL copied = BitBlt(memoryDc,
                             0,
                             0,
                             width,
                             height,
                             screenDc,
                             topLeft.x,
                             topLeft.y,
                             SRCCOPY);

  BITMAPINFO info {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
  const int rows = copied ? GetDIBits(screenDc,
                                      bitmap,
                                      0,
                                      static_cast<UINT>(height),
                                      pixels.data(),
                                      &info,
                                      DIB_RGB_COLORS)
                          : 0;

  SelectObject(memoryDc, oldBitmap);
  DeleteObject(bitmap);
  DeleteDC(memoryDc);
  ReleaseDC(nullptr, screenDc);

  if (rows == 0) {
    return false;
  }

  int nonBlackSamples = 0;
  int totalSamples = 0;
  for (int y = height / 5; y < (height * 4) / 5; y += 12) {
    for (int x = width / 5; x < (width * 4) / 5; x += 12) {
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      const int blue = pixels[offset + 0];
      const int green = pixels[offset + 1];
      const int red = pixels[offset + 2];
      ++totalSamples;
      if (red + green + blue > 60) {
        ++nonBlackSamples;
      }
    }
  }

  return totalSamples > 0 && nonBlackSamples > totalSamples / 20;
}

void LaunchRuntimeChild(const RuntimeFixturePaths& paths,
                        bool headless,
                        const char* roleFlag,
                        int connectPort,
                        ManagedChildProcess& child) {
  const std::string exe = GenericPath(paths.runtimeExe);
  std::string commandLine = "\"" + exe + "\" -networkPlayManifest=\"" +
                            GenericPath(paths.manifestPath) +
                            "\" " + roleFlag +
                            " -connectHost=\"127.0.0.1\" -connectPort=" +
                            std::to_string(connectPort);
  if (headless) {
    commandLine += " -headless";
  }

  std::wstring mutableCommandLine = Utf8ToWide(commandLine);
  mutableCommandLine.push_back(L'\0');

  STARTUPINFOW startupInfo {};
  startupInfo.cb = sizeof(startupInfo);
  startupInfo.dwFlags = STARTF_USESHOWWINDOW;
  startupInfo.wShowWindow = headless ? SW_HIDE : SW_SHOWNORMAL;

  PROCESS_INFORMATION processInfo {};
  const std::wstring executable = Utf8ToWide(exe);
  const std::wstring workingDirectory =
      Utf8ToWide(GenericPath(paths.runtimeExe.parent_path()));

  ASSERT_TRUE(CreateProcessW(executable.c_str(),
                             mutableCommandLine.data(),
                             nullptr,
                             nullptr,
                             FALSE,
                             0,
                             nullptr,
                             workingDirectory.c_str(),
                             &startupInfo,
                             &processInfo))
      << "CreateProcessW failed with error " << GetLastError();

  CloseHandle(processInfo.hThread);
  child.processHandle = processInfo.hProcess;
  child.processId = processInfo.dwProcessId;
  ASSERT_NE(child.processHandle, nullptr);
  ASSERT_NE(child.processId, 0u);
}

bool RuntimeFixtureExists(const RuntimeFixturePaths& paths,
                          std::string& skipReason) {
  if (!std::filesystem::exists(paths.runtimeExe)) {
    skipReason = "Runtime executable not built: " + paths.runtimeExe.string();
    return false;
  }

  if (!std::filesystem::exists(paths.scenePath)) {
    skipReason = "Network test scene missing: " + paths.scenePath.string();
    return false;
  }

  return true;
}

std::string ReadTextFileIfExists(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::in);
  if (!file.is_open()) {
    return {};
  }

  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

bool TextContainsAll(const std::string& text,
                     std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (text.find(needle) == std::string::npos) {
      return false;
    }
  }

  return true;
}

#endif

TEST(NetworkPlayChildProcessSmokeTests,
     HeadlessClientBootsFromGeneratedManifestAndStaysAlive) {
#ifndef _WIN32
  GTEST_SKIP() << "Editor runtime child process smoke tests are Windows-only.";
#else
  const RuntimeFixturePaths paths = BuildFixturePaths("headless-client");
  std::string skipReason;
  if (!RuntimeFixtureExists(paths, skipReason)) {
    GTEST_SKIP() << skipReason;
  }
  PrepareFixtureFiles(paths);
  WriteManifest(paths, "headless-client", true);

  ManagedChildProcess child;
  LaunchRuntimeChild(paths, true, "-client", 7777, child);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (std::chrono::steady_clock::now() < deadline) {
    ASSERT_FALSE(ProcessExited(child))
        << "Headless child exited early with code "
        << GetProcessExitCode(child);

    const WindowSearch windows = FindProcessWindows(child);
    ASSERT_EQ(windows.errorWindow, nullptr)
        << "Headless child opened fatal dialog: "
        << WideToUtf8(windows.errorTitle);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
#endif
}

TEST(NetworkPlayChildProcessSmokeTests,
     VisibleClientBootsFromGeneratedManifestAndPresentsScenePixels) {
#ifndef _WIN32
  GTEST_SKIP() << "Editor runtime child process smoke tests are Windows-only.";
#else
  const RuntimeFixturePaths paths = BuildFixturePaths("visible-client");
  std::string skipReason;
  if (!RuntimeFixtureExists(paths, skipReason)) {
    GTEST_SKIP() << skipReason;
  }
  PrepareFixtureFiles(paths);
  WriteManifest(paths, "visible-client", false);

  ManagedChildProcess child;
  LaunchRuntimeChild(paths, false, "-client", 7777, child);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (std::chrono::steady_clock::now() < deadline) {
    ASSERT_FALSE(ProcessExited(child))
        << "Visible child exited early with code "
        << GetProcessExitCode(child);

    const WindowSearch windows = FindProcessWindows(child);
    ASSERT_EQ(windows.errorWindow, nullptr)
        << "Visible child opened fatal dialog: "
        << WideToUtf8(windows.errorTitle);

    if (windows.runtimeWindow != nullptr) {
      ShowWindow(windows.runtimeWindow, SW_RESTORE);
      SetWindowPos(windows.runtimeWindow,
                   HWND_TOP,
                   64,
                   64,
                   960,
                   540,
                   SWP_SHOWWINDOW);
      std::this_thread::sleep_for(std::chrono::milliseconds(250));

      if (CaptureClientAreaHasNonBlackPixels(windows.runtimeWindow)) {
        return;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  FAIL() << "Visible child stayed alive but did not present non-black scene pixels.";
#endif
}

TEST(NetworkPlayChildProcessSmokeTests,
     HeadlessServerAndClientHandshakeSpawnsPlayerPrefab) {
#ifndef _WIN32
  GTEST_SKIP() << "Editor runtime child process smoke tests are Windows-only.";
#else
  const RuntimeFixturePaths serverPaths = BuildFixturePaths("spawn-server");
  const RuntimeFixturePaths clientPaths = BuildFixturePaths("spawn-client");
  std::string skipReason;
  if (!RuntimeFixtureExists(serverPaths, skipReason)) {
    GTEST_SKIP() << skipReason;
  }
  if (!RuntimeFixtureExists(clientPaths, skipReason)) {
    GTEST_SKIP() << skipReason;
  }

  constexpr int port = 7791;
  PrepareFixtureFiles(serverPaths);
  PrepareFixtureFiles(clientPaths);
  WriteManifest(serverPaths, "spawn-server", true, "DedicatedServer", port, port);
  WriteManifest(clientPaths, "spawn-client", true, "Client", port, 0);

  ManagedChildProcess server;
  LaunchRuntimeChild(serverPaths, true, "-server", port, server);

  const auto serverReadyDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < serverReadyDeadline) {
    ASSERT_FALSE(ProcessExited(server))
        << "Server child exited early with code " << GetProcessExitCode(server);

    const std::string serverLog =
        ReadTextFileIfExists(serverPaths.testRoot / "Logs" / "Log.txt");
    if (serverLog.find("Started as server on port") != std::string::npos) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  ManagedChildProcess client;
  LaunchRuntimeChild(clientPaths, true, "-client", port, client);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (std::chrono::steady_clock::now() < deadline) {
    ASSERT_FALSE(ProcessExited(server))
        << "Server child exited early with code " << GetProcessExitCode(server);
    ASSERT_FALSE(ProcessExited(client))
        << "Client child exited early with code " << GetProcessExitCode(client);

    const WindowSearch serverWindows = FindProcessWindows(server);
    ASSERT_EQ(serverWindows.errorWindow, nullptr)
        << "Server child opened fatal dialog: "
        << WideToUtf8(serverWindows.errorTitle);

    const WindowSearch clientWindows = FindProcessWindows(client);
    ASSERT_EQ(clientWindows.errorWindow, nullptr)
        << "Client child opened fatal dialog: "
        << WideToUtf8(clientWindows.errorTitle);

    const std::string serverLog =
        ReadTextFileIfExists(serverPaths.testRoot / "Logs" / "Log.txt");
    const std::string clientLog =
        ReadTextFileIfExists(clientPaths.testRoot / "Logs" / "Log.txt");

    if (TextContainsAll(serverLog,
                        {"Started as server on port",
                         "Server: New client has connected",
                         "NetworkPlayer Constructed",
                         "NetworkComponent Registered"}) &&
        clientLog.find("Started as client connecting to 127.0.0.1") !=
            std::string::npos) {
      return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }

  FAIL() << "Server/client children stayed alive but did not log player spawn. "
         << "Server log: "
         << ReadTextFileIfExists(serverPaths.testRoot / "Logs" / "Log.txt")
         << " Client log: "
         << ReadTextFileIfExists(clientPaths.testRoot / "Logs" / "Log.txt");
#endif
}

} // namespace
