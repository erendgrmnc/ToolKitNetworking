#pragma once

#include "INetworkSessionRuntime.h"
#include "NetworkMacros.h"
#include "NetworkSessionCore.h"
#include <functional>
#include <string>

namespace ToolKit::ToolKitNetworking {
class NetworkSessionManager {
public:
  using CommandLineOverridesProvider =
      std::function<CommandLineSessionOverrides()>;
  using ClockNowProvider = std::function<uint64_t()>;

  explicit NetworkSessionManager(
      INetworkSessionRuntime &runtime,
      CommandLineOverridesProvider commandLineOverridesProvider = {},
      ClockNowProvider clockNowProvider = {});
  ~NetworkSessionManager() = default;

  bool StartConfiguredSession();
  void StopSession(DisconnectReason reason = DisconnectReason::UserRequested);
  void Update();

  HostingMode ResolveHostingMode() const;
  const ConnectionStatus &GetConnectionStatus() const;
  const SessionDescriptor &GetActiveSession() const;
  const SessionHostRequest &GetLastHostRequest() const;
  const SessionJoinRequest &GetLastJoinRequest() const;

private:
  CommandLineSessionOverrides GetRuntimeCommandLineOverrides() const;
  HostingMode ResolveConfiguredHostingMode() const;
  uint64_t GetNowMs() const;
  void SetStatus(ConnectionState state,
                 DisconnectReason reason = DisconnectReason::None,
                 const std::string &detail = "");

private:
  INetworkSessionRuntime &m_runtime;
  CommandLineOverridesProvider m_commandLineOverridesProvider;
  ClockNowProvider m_clockNowProvider;
  ConnectionStatus m_connectionStatus;
  SessionDescriptor m_activeSession;
  SessionHostRequest m_lastHostRequest;
  SessionJoinRequest m_lastJoinRequest;
  uint64_t m_connectionAttemptStartedAtMs = 0;
};
} // namespace ToolKit::ToolKitNetworking
