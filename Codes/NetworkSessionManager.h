#pragma once

#include "INetworkSessionRuntime.h"
#include "NetworkMacros.h"
#include "NetworkSessionCore.h"
#include "SessionBootstrapProvider.h"
#include <functional>
#include <memory>
#include <string>

namespace ToolKit::ToolKitNetworking {
class NetworkSessionManager {
public:
  using CommandLineOverridesProvider =
      std::function<CommandLineSessionOverrides()>;
  using ClockNowProvider = std::function<uint64_t()>;
  using BootstrapProviderFactory =
      std::function<SessionBootstrapProviderPtr(JoinMethod)>;

  explicit NetworkSessionManager(
      INetworkSessionRuntime &runtime,
      CommandLineOverridesProvider commandLineOverridesProvider = {},
      ClockNowProvider clockNowProvider = {},
      BootstrapProviderFactory bootstrapProviderFactory = {});
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
  void ApplyResolvedSession(const SessionDescriptor &resolvedSession);
  void SetStatus(ConnectionState state,
                 DisconnectReason reason = DisconnectReason::None,
                 const std::string &detail = "");

private:
  INetworkSessionRuntime &m_runtime;
  CommandLineOverridesProvider m_commandLineOverridesProvider;
  ClockNowProvider m_clockNowProvider;
  BootstrapProviderFactory m_bootstrapProviderFactory;
  ConnectionStatus m_connectionStatus;
  SessionDescriptor m_activeSession;
  SessionHostRequest m_lastHostRequest;
  SessionJoinRequest m_lastJoinRequest;
  uint64_t m_connectionAttemptStartedAtMs = 0;
  uint64_t m_handshakeStartedAtMs = 0;
};
} // namespace ToolKit::ToolKitNetworking
