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
  ~NetworkSessionManager();

  bool StartConfiguredSession();
  void StopSession(DisconnectReason reason = DisconnectReason::UserRequested);
  void Update();

  HostingMode ResolveHostingMode() const;
  const ConnectionStatus &GetConnectionStatus() const;
  const SessionDescriptor &GetActiveSession() const;
  const SessionHostRequest &GetLastHostRequest() const;
  const SessionJoinRequest &GetLastJoinRequest() const;

private:
  struct HostedRegistrationState {
    SessionBootstrapProviderPtr provider;
    SessionHostRequest releaseRequest;
    String registrationHandle;
    uint64_t registrationLeaseIssuedAtMs = 0;
    uint64_t registrationLeaseExpiresAtMs = 0;
    bool releasePending = false;
    bool releaseBlocked = false;
    uint64_t nextReleaseRetryAtMs = 0;
    DisconnectReason lastReleaseFailureReason = DisconnectReason::None;
    String lastReleaseFailureDetail;
  };

  struct HostedReleaseResult {
    bool attempted = false;
    bool released = false;
    bool retryableFailure = false;
    DisconnectReason disconnectReason = DisconnectReason::None;
    String detailMessage;
  };

  CommandLineSessionOverrides GetRuntimeCommandLineOverrides() const;
  HostingMode ResolveConfiguredHostingMode() const;
  uint64_t GetNowMs() const;
  void ApplyResolvedSession(const SessionDescriptor &resolvedSession);
  bool HasHostedSessionRegistration() const;
  void CaptureHostedSessionRegistration(SessionBootstrapProviderPtr provider,
                                        const SessionHostRequest &request);
  void ClearHostedSessionRegistrationState();
  bool TryClearPendingHostedRegistrationForStart();
  HostedReleaseResult ReleaseHostedSessionRegistration(bool forceAttempt,
                                                       bool allowBlockedRetry = false);
  void RetryPendingHostedSessionReleaseIfDue();
  bool RefreshHostedSessionRegistration();
  uint64_t ComputeHostedSessionRefreshAtMs() const;
  static bool IsTerminalHostedReleaseFailure(DisconnectReason reason);
  static bool IsRetryableHostedReleaseFailure(DisconnectReason reason);
  void ResetDiagnostics();
  void SetBootstrapFailureStatus(DisconnectReason reason,
                                 const std::string &detail);
  void SetHandshakeFailureStatus(DisconnectReason reason,
                                 const std::string &detail);
  void RefreshEndpointDiagnostics();
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
  HostedRegistrationState m_hostedRegistration;
  uint64_t m_connectionAttemptStartedAtMs = 0;
  uint64_t m_handshakeStartedAtMs = 0;
};
} // namespace ToolKit::ToolKitNetworking
