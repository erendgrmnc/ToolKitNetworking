#include "NetworkSessionManager.h"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ToolKit::ToolKitNetworking {
namespace {
constexpr uint64_t HostedRegistrationReleaseRetryMs = 5000;

CommandLineSessionOverrides ReadProcessCommandLineOverrides() {
#ifdef _WIN32
  const char *commandLine = GetCommandLineA();
  if (commandLine != nullptr) {
    return SessionCore::ParseCommandLineOverrides(commandLine);
  }
#endif
  return CommandLineSessionOverrides{};
}

std::vector<String> CollectSecrets(const SessionHostRequest &hostRequest,
                                   const SessionJoinRequest &joinRequest) {
  std::vector<String> secrets;
  if (!hostRequest.joinCredential.empty()) {
    secrets.push_back(hostRequest.joinCredential);
  }
  if (!joinRequest.joinCredential.empty()) {
    secrets.push_back(joinRequest.joinCredential);
  }

  return secrets;
}

String AppendWarningMessage(const String &detail, const String &warning) {
  if (warning.empty()) {
    return detail;
  }
  if (detail.empty()) {
    return warning;
  }
  return detail + " Warning: " + warning;
}
} // namespace

NetworkSessionManager::NetworkSessionManager(
    INetworkSessionRuntime &runtime,
    CommandLineOverridesProvider commandLineOverridesProvider,
    ClockNowProvider clockNowProvider,
    BootstrapProviderFactory bootstrapProviderFactory)
    : m_runtime(runtime),
      m_commandLineOverridesProvider(commandLineOverridesProvider),
      m_clockNowProvider(clockNowProvider),
      m_bootstrapProviderFactory(bootstrapProviderFactory) {
  if (!m_commandLineOverridesProvider) {
    m_commandLineOverridesProvider = []() {
      return ReadProcessCommandLineOverrides();
    };
  }
  if (!m_clockNowProvider) {
    m_clockNowProvider = []() {
      const auto now = std::chrono::steady_clock::now().time_since_epoch();
      return static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    };
  }
  if (!m_bootstrapProviderFactory) {
    m_bootstrapProviderFactory = [](JoinMethod joinMethod) {
      return CreateBootstrapProvider(joinMethod);
    };
  }

  m_connectionStatus = ConnectionStatus{};
  m_connectionStatus.detailMessage = "Idle";
}

NetworkSessionManager::~NetworkSessionManager() {
  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }
  (void)ReleaseHostedSessionRegistration(true, true);
  ClearHostedSessionRegistrationState();
}

bool NetworkSessionManager::StartConfiguredSession() {
  const CommandLineSessionOverrides overrides =
      m_commandLineOverridesProvider();
  const HostingMode hostingMode =
      overrides.hasHostingModeOverride ? overrides.hostingMode
                                       : ResolveConfiguredHostingMode();
  SessionBootstrapConfig config = m_runtime.GetSessionBootstrapConfig();
  config.hostingMode = hostingMode;

  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }
  if (!TryClearPendingHostedRegistrationForStart()) {
    return false;
  }

  m_lastHostRequest = SessionHostRequest{};
  m_lastJoinRequest = SessionJoinRequest{};
  m_activeSession = SessionDescriptor{};
  m_activeSession.hostingMode = hostingMode;
  m_activeSession.sessionId = m_lastHostRequest.sessionId;
  ResetDiagnostics();

  const bool shouldHost = hostingMode == HostingMode::DedicatedServer ||
                          hostingMode == HostingMode::ListenServer;
  const bool shouldJoin = hostingMode == HostingMode::Client ||
                          hostingMode == HostingMode::ListenServer;

  if (!shouldHost && !shouldJoin) {
    m_connectionAttemptStartedAtMs = 0;

    SetStatus(ConnectionState::Idle, DisconnectReason::None,
              "Networking disabled for current hosting mode.");
    return true;
  }

  SessionBootstrapProviderPtr bootstrapProvider =
      m_bootstrapProviderFactory(config.joinMethod);
  if (!bootstrapProvider) {
    SetBootstrapFailureStatus(
        DisconnectReason::BootstrapFailed,
        "No bootstrap provider is available for the selected join method.");
    return false;
  }
  ISessionBootstrapProvider *bootstrapProviderInterface = bootstrapProvider.get();

  if (shouldHost) {
    m_lastHostRequest = SessionCore::BuildHostRequest(config, overrides);
    if (config.joinMethod == JoinMethod::SessionDirectory) {
      m_lastHostRequest.requireJoinCredential = true;
    }
    const BootstrapHostResult bootstrapHost =
        bootstrapProvider->PrepareHostSession(m_lastHostRequest);
    if (!bootstrapHost.success) {
      const String detail = SessionCore::SanitizeDiagnosticDetail(
          bootstrapHost.detailMessage.empty()
              ? "Failed to prepare host bootstrap session."
              : bootstrapHost.detailMessage,
          CollectSecrets(bootstrapHost.request, m_lastJoinRequest));
      SetBootstrapFailureStatus(bootstrapHost.disconnectReason, detail);
      return false;
    }

    m_lastHostRequest = bootstrapHost.request;
    ApplyResolvedSession(bootstrapHost.session);
    if (!m_lastHostRequest.directoryRegistrationHandle.empty()) {
      CaptureHostedSessionRegistration(std::move(bootstrapProvider),
                                       m_lastHostRequest);
      bootstrapProviderInterface = m_hostedRegistration.provider.get();
    }
    const SessionValidationResult hostValidation =
        SessionCore::ValidateHostBootstrapResult(m_lastHostRequest);
    if (!hostValidation.success) {
      ReleaseHostedSessionRegistration(true);
      SetBootstrapFailureStatus(
          hostValidation.disconnectReason,
          SessionCore::SanitizeDiagnosticDetail(
              hostValidation.detailMessage,
              CollectSecrets(m_lastHostRequest, m_lastJoinRequest)));
      return false;
    }
    m_activeSession.isJoinCredentialRequired =
        m_activeSession.isJoinCredentialRequired ||
        m_lastHostRequest.requireJoinCredential;

    SetStatus(ConnectionState::StartingHost);
    if (!m_runtime.StartServerTransport(m_lastHostRequest.bindEndpoint.port)) {
      ReleaseHostedSessionRegistration(true);
      SetStatus(ConnectionState::Failed, DisconnectReason::TransportError,
                "Failed to start server transport.");
      return false;
    }
  }

  if (shouldJoin) {
    m_lastJoinRequest = SessionCore::BuildJoinRequest(config, overrides);
    const BootstrapJoinResult bootstrapJoin =
        bootstrapProviderInterface->ResolveJoinSession(m_lastJoinRequest,
                                                       hostingMode);
    if (!bootstrapJoin.success) {
      if (shouldHost) {
        m_runtime.StopSessionTransports();
        ReleaseHostedSessionRegistration(true);
      }

      const String detail = SessionCore::SanitizeDiagnosticDetail(
          bootstrapJoin.detailMessage.empty()
              ? "Failed to resolve join bootstrap session."
              : bootstrapJoin.detailMessage,
          CollectSecrets(m_lastHostRequest, bootstrapJoin.request));
      SetBootstrapFailureStatus(bootstrapJoin.disconnectReason, detail);
      return false;
    }

    m_lastJoinRequest = bootstrapJoin.request;
    ApplyResolvedSession(bootstrapJoin.session);
    const SessionValidationResult joinValidation =
        SessionCore::ValidateJoinBootstrapResult(m_lastJoinRequest, m_activeSession);
    if (!joinValidation.success) {
      if (shouldHost) {
        m_runtime.StopSessionTransports();
        ReleaseHostedSessionRegistration(true);
      }
      SetBootstrapFailureStatus(
          joinValidation.disconnectReason,
          SessionCore::SanitizeDiagnosticDetail(
              joinValidation.detailMessage,
              CollectSecrets(m_lastHostRequest, m_lastJoinRequest)));
      return false;
    }
    m_activeSession.isJoinCredentialRequired =
        m_activeSession.isJoinCredentialRequired ||
        !m_lastJoinRequest.joinCredential.empty();
    m_connectionAttemptStartedAtMs = GetNowMs();
    m_handshakeStartedAtMs = 0;

    SetStatus(ConnectionState::Connecting);
    const NetworkEndpoint &resolvedEndpoint = m_activeSession.resolvedEndpoint;
    if (!m_runtime.StartClientTransport(resolvedEndpoint.host,
                                        resolvedEndpoint.port)) {
      if (shouldHost) {
        m_runtime.StopSessionTransports();
        ReleaseHostedSessionRegistration(true);
      }
      m_connectionAttemptStartedAtMs = 0;
      m_handshakeStartedAtMs = 0;

      SetStatus(ConnectionState::Failed, DisconnectReason::TransportError,
                "Failed to create client connection.");
      return false;
    }

    if (hostingMode == HostingMode::ListenServer) {
      SetStatus(ConnectionState::Connecting, DisconnectReason::None,
                "Listen server is waiting for local client handshake.");
    }
  } else {
    m_connectionAttemptStartedAtMs = 0;
    m_handshakeStartedAtMs = 0;
    SetStatus(ConnectionState::Connected, DisconnectReason::None,
              "Dedicated server listening for incoming clients.");
  }

  return true;
}

void NetworkSessionManager::StopSession(DisconnectReason reason) {
  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }

  const HostedReleaseResult releaseResult =
      ReleaseHostedSessionRegistration(true);
  String detail = reason == DisconnectReason::UserRequested
                      ? "Session stopped by user request."
                      : "Session stopped.";
  if (!releaseResult.released && !releaseResult.detailMessage.empty()) {
    detail = AppendWarningMessage(detail, releaseResult.detailMessage);
  }

  SetStatus(ConnectionState::Disconnected, reason, detail);

  m_connectionAttemptStartedAtMs = 0;
  m_handshakeStartedAtMs = 0;
  m_activeSession = SessionDescriptor{};
  m_lastHostRequest = SessionHostRequest{};
  m_lastJoinRequest = SessionJoinRequest{};
}

void NetworkSessionManager::Update() {
  RetryPendingHostedSessionReleaseIfDue();
  if (!RefreshHostedSessionRegistration()) {
    return;
  }

  if (m_connectionStatus.state == ConnectionState::Connecting &&
      m_lastJoinRequest.connectionTimeoutMs > 0) {
    const uint64_t elapsedMs = GetNowMs() - m_connectionAttemptStartedAtMs;
    if (elapsedMs >= m_lastJoinRequest.connectionTimeoutMs) {
      if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
        m_runtime.StopSessionTransports();
      }
      ReleaseHostedSessionRegistration(true);

      m_connectionAttemptStartedAtMs = 0;
      m_handshakeStartedAtMs = 0;
      SetStatus(ConnectionState::Failed, DisconnectReason::Timeout,
                "Client connection timed out.");
      return;
    }
  }

  if (m_connectionStatus.state == ConnectionState::Connecting &&
      m_runtime.HasClientTransport() && m_runtime.IsClientTransportConnected()) {
    if (!m_runtime.BeginSessionHandshake(m_lastJoinRequest)) {
      if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
        m_runtime.StopSessionTransports();
      }
      ReleaseHostedSessionRegistration(true);
      m_connectionAttemptStartedAtMs = 0;
      m_handshakeStartedAtMs = 0;
      SetHandshakeFailureStatus(
          m_runtime.GetSessionAuthFailureReason(),
          SessionCore::SanitizeDiagnosticDetail(
              m_runtime.GetSessionAuthFailureDetail().empty()
                  ? "Failed to start session handshake."
                  : m_runtime.GetSessionAuthFailureDetail(),
              CollectSecrets(m_lastHostRequest, m_lastJoinRequest)));
      return;
    }

    m_connectionAttemptStartedAtMs = 0;
    m_handshakeStartedAtMs = GetNowMs();
    SetStatus(ConnectionState::Handshaking, DisconnectReason::None,
              "Waiting for session handshake.");
    return;
  }

  if (m_connectionStatus.state == ConnectionState::Handshaking &&
      m_runtime.HasSessionAuthFailed()) {
    if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
      m_runtime.StopSessionTransports();
    }
    ReleaseHostedSessionRegistration(true);
    m_handshakeStartedAtMs = 0;
    SetHandshakeFailureStatus(
        m_runtime.GetSessionAuthFailureReason(),
        SessionCore::SanitizeDiagnosticDetail(
            m_runtime.GetSessionAuthFailureDetail().empty()
                ? "Session handshake rejected."
                : m_runtime.GetSessionAuthFailureDetail(),
            CollectSecrets(m_lastHostRequest, m_lastJoinRequest)));
    return;
  }

  if (m_connectionStatus.state == ConnectionState::Handshaking &&
      m_lastJoinRequest.handshakeTimeoutMs > 0 &&
      m_handshakeStartedAtMs != 0) {
    const uint64_t elapsedMs = GetNowMs() - m_handshakeStartedAtMs;
    if (elapsedMs >= m_lastJoinRequest.handshakeTimeoutMs) {
      if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
        m_runtime.StopSessionTransports();
      }
      ReleaseHostedSessionRegistration(true);

      m_handshakeStartedAtMs = 0;
      SetHandshakeFailureStatus(DisconnectReason::Timeout,
                                "Session handshake timed out.");
      return;
    }
  }

  if (m_connectionStatus.state == ConnectionState::Handshaking &&
      m_runtime.IsSessionAuthenticated()) {
    m_handshakeStartedAtMs = 0;
    if (ResolveHostingMode() == HostingMode::ListenServer) {
      SetStatus(ConnectionState::Connected, DisconnectReason::None,
                "Listen server and local client are authenticated.");
    } else {
      SetStatus(ConnectionState::Connected, DisconnectReason::None,
                "Client authenticated.");
    }
    return;
  }

  if (m_connectionStatus.state == ConnectionState::Connected &&
      m_runtime.HasClientTransport() && !m_runtime.IsClientTransportConnected() &&
      !m_runtime.HasServerTransport()) {
    m_connectionAttemptStartedAtMs = 0;
    m_handshakeStartedAtMs = 0;
    SetStatus(ConnectionState::Disconnected, DisconnectReason::TransportError,
              "Client transport disconnected.");
  }
}

HostingMode NetworkSessionManager::ResolveHostingMode() const {
  if (m_connectionStatus.state == ConnectionState::Idle ||
      m_connectionStatus.state == ConnectionState::Disconnected ||
      m_connectionStatus.state == ConnectionState::Failed) {
    return ResolveConfiguredHostingMode();
  }

  return m_activeSession.hostingMode;
}

const ConnectionStatus &NetworkSessionManager::GetConnectionStatus() const {
  return m_connectionStatus;
}

const SessionDescriptor &NetworkSessionManager::GetActiveSession() const {
  return m_activeSession;
}

const SessionHostRequest &NetworkSessionManager::GetLastHostRequest() const {
  return m_lastHostRequest;
}

const SessionJoinRequest &NetworkSessionManager::GetLastJoinRequest() const {
  return m_lastJoinRequest;
}

CommandLineSessionOverrides
NetworkSessionManager::GetRuntimeCommandLineOverrides() const {
  return m_commandLineOverridesProvider();
}

HostingMode NetworkSessionManager::ResolveConfiguredHostingMode() const {
  return m_runtime.GetSessionBootstrapConfig().hostingMode;
}

uint64_t NetworkSessionManager::GetNowMs() const { return m_clockNowProvider(); }

void NetworkSessionManager::ApplyResolvedSession(
    const SessionDescriptor &resolvedSession) {
  if (!resolvedSession.sessionId.empty()) {
    m_activeSession.sessionId = resolvedSession.sessionId;
  }
  if (resolvedSession.hostingMode != HostingMode::None) {
    m_activeSession.hostingMode = resolvedSession.hostingMode;
  }

  m_activeSession.joinMethod = resolvedSession.joinMethod;

  if (resolvedSession.bindEndpoint.IsConfigured() ||
      (!resolvedSession.bindEndpoint.host.empty() &&
       resolvedSession.bindEndpoint.port == 0)) {
    m_activeSession.bindEndpoint = resolvedSession.bindEndpoint;
  }
  if (resolvedSession.advertisedEndpoint.IsConfigured() ||
      (!resolvedSession.advertisedEndpoint.host.empty() &&
       resolvedSession.advertisedEndpoint.port == 0)) {
    m_activeSession.advertisedEndpoint = resolvedSession.advertisedEndpoint;
  }
  if (resolvedSession.resolvedEndpoint.IsConfigured() ||
      (!resolvedSession.resolvedEndpoint.host.empty() &&
       resolvedSession.resolvedEndpoint.port == 0)) {
    m_activeSession.resolvedEndpoint = resolvedSession.resolvedEndpoint;
  }
  if (!resolvedSession.buildCompatibilityId.empty()) {
    m_activeSession.buildCompatibilityId = resolvedSession.buildCompatibilityId;
  }

  m_activeSession.relayRequired = resolvedSession.relayRequired;
  m_activeSession.isJoinCredentialRequired =
      m_activeSession.isJoinCredentialRequired ||
      resolvedSession.isJoinCredentialRequired;
}

bool NetworkSessionManager::HasHostedSessionRegistration() const {
  return !m_hostedRegistration.registrationHandle.empty() &&
         m_hostedRegistration.provider != nullptr;
}

void NetworkSessionManager::CaptureHostedSessionRegistration(
    SessionBootstrapProviderPtr provider, const SessionHostRequest &request) {
  m_hostedRegistration = HostedRegistrationState{};
  if (!provider || request.directoryRegistrationHandle.empty()) {
    return;
  }

  m_hostedRegistration.provider = std::move(provider);
  m_hostedRegistration.releaseRequest = request;
  m_hostedRegistration.registrationHandle = request.directoryRegistrationHandle;
  m_hostedRegistration.registrationLeaseIssuedAtMs = GetNowMs();
  m_hostedRegistration.registrationLeaseExpiresAtMs =
      request.directoryRegistrationExpiresAtMs;
  m_hostedRegistration.releasePending = false;
  m_hostedRegistration.nextReleaseRetryAtMs = 0;
  m_hostedRegistration.lastReleaseFailureReason = DisconnectReason::None;
  m_hostedRegistration.lastReleaseFailureDetail.clear();
}

void NetworkSessionManager::ClearHostedSessionRegistrationState() {
  m_hostedRegistration = HostedRegistrationState{};
  m_lastHostRequest.directoryRegistrationHandle.clear();
  m_lastHostRequest.directoryRegistrationExpiresAtMs = 0;
}

bool NetworkSessionManager::TryClearPendingHostedRegistrationForStart() {
  if (!HasHostedSessionRegistration()) {
    return true;
  }

  const SessionHostRequest releaseRequest = m_hostedRegistration.releaseRequest;
  if (m_hostedRegistration.releaseBlocked) {
    SetBootstrapFailureStatus(
        DisconnectReason::BootstrapFailed,
        SessionCore::SanitizeDiagnosticDetail(
            m_hostedRegistration.lastReleaseFailureDetail.empty()
                ? "The previous hosted session registration is blocked and must "
                  "be cleared before starting a new session."
                : m_hostedRegistration.lastReleaseFailureDetail,
            CollectSecrets(releaseRequest, m_lastJoinRequest)));
    return false;
  }

  const HostedReleaseResult releaseResult = ReleaseHostedSessionRegistration(true);
  if (releaseResult.released) {
    return true;
  }

  SetBootstrapFailureStatus(
      DisconnectReason::BootstrapFailed,
      SessionCore::SanitizeDiagnosticDetail(
          releaseResult.detailMessage.empty()
              ? "Failed to release the previous hosted session registration."
              : releaseResult.detailMessage,
          CollectSecrets(releaseRequest, m_lastJoinRequest)));
  return false;
}

NetworkSessionManager::HostedReleaseResult
NetworkSessionManager::ReleaseHostedSessionRegistration(bool forceAttempt,
                                                        bool allowBlockedRetry) {
  HostedReleaseResult result;
  if (!HasHostedSessionRegistration()) {
    return result;
  }

  if (m_hostedRegistration.releaseBlocked && !allowBlockedRetry) {
    result.disconnectReason = m_hostedRegistration.lastReleaseFailureReason;
    result.detailMessage = m_hostedRegistration.lastReleaseFailureDetail;
    return result;
  }

  const uint64_t nowMs = GetNowMs();
  if (m_hostedRegistration.releasePending && !forceAttempt &&
      nowMs < m_hostedRegistration.nextReleaseRetryAtMs) {
    return result;
  }

  result.attempted = true;
  const HostedSessionReleaseResult releaseResult =
      m_hostedRegistration.provider->ReleaseHostedSession(
          m_hostedRegistration.releaseRequest);

  if (releaseResult.success ||
      releaseResult.disconnectReason == DisconnectReason::SessionClosed) {
    result.released = true;
    ClearHostedSessionRegistrationState();
    return result;
  }

  result.disconnectReason = releaseResult.disconnectReason;
  result.detailMessage = SessionCore::SanitizeDiagnosticDetail(
      releaseResult.detailMessage.empty()
          ? "Failed to release the hosted session directory registration."
          : releaseResult.detailMessage,
      CollectSecrets(m_hostedRegistration.releaseRequest, m_lastJoinRequest));

  if (IsRetryableHostedReleaseFailure(releaseResult.disconnectReason)) {
    result.retryableFailure = true;
    m_hostedRegistration.releasePending = true;
    m_hostedRegistration.releaseBlocked = false;
    m_hostedRegistration.nextReleaseRetryAtMs =
        nowMs + HostedRegistrationReleaseRetryMs;
    m_hostedRegistration.lastReleaseFailureReason =
        releaseResult.disconnectReason;
    m_hostedRegistration.lastReleaseFailureDetail = result.detailMessage;
    return result;
  }

  if (!IsTerminalHostedReleaseFailure(releaseResult.disconnectReason)) {
    result.disconnectReason = DisconnectReason::ProtocolError;
  }
  m_hostedRegistration.releaseBlocked = true;
  m_hostedRegistration.releasePending = false;
  m_hostedRegistration.nextReleaseRetryAtMs = 0;
  m_hostedRegistration.lastReleaseFailureReason = result.disconnectReason;
  m_hostedRegistration.lastReleaseFailureDetail = result.detailMessage;
  return result;
}

void NetworkSessionManager::RetryPendingHostedSessionReleaseIfDue() {
  if (!HasHostedSessionRegistration() || !m_hostedRegistration.releasePending ||
      m_hostedRegistration.releaseBlocked) {
    return;
  }

  (void)ReleaseHostedSessionRegistration(false);
}

bool NetworkSessionManager::RefreshHostedSessionRegistration() {
  if (!HasHostedSessionRegistration() || m_hostedRegistration.releasePending ||
      m_hostedRegistration.registrationLeaseExpiresAtMs == 0) {
    return true;
  }

  const uint64_t nowMs = GetNowMs();
  const uint64_t refreshAtMs = ComputeHostedSessionRefreshAtMs();
  if (nowMs < refreshAtMs) {
    return true;
  }

  const HostedSessionRefreshResult refreshResult =
      m_hostedRegistration.provider->RefreshHostedSession(
          m_hostedRegistration.releaseRequest);
  if (!refreshResult.success) {
    const SessionHostRequest releaseRequest = m_hostedRegistration.releaseRequest;
    if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
      m_runtime.StopSessionTransports();
    }
    const HostedReleaseResult releaseResult =
        ReleaseHostedSessionRegistration(true);
    m_connectionAttemptStartedAtMs = 0;
    m_handshakeStartedAtMs = 0;
    SetBootstrapFailureStatus(
        refreshResult.disconnectReason,
        SessionCore::SanitizeDiagnosticDetail(
            refreshResult.detailMessage.empty()
                ? "Failed to refresh hosted session directory registration."
                : refreshResult.detailMessage,
            CollectSecrets(releaseRequest, m_lastJoinRequest)));
    if (!releaseResult.released && !releaseResult.detailMessage.empty()) {
      m_connectionStatus.detailMessage = AppendWarningMessage(
          m_connectionStatus.detailMessage, releaseResult.detailMessage);
      m_connectionStatus.bootstrapDetail = m_connectionStatus.detailMessage;
    }
    return false;
  }

  if (refreshResult.registrationExpiresAtMs != 0) {
    m_hostedRegistration.registrationLeaseIssuedAtMs = nowMs;
    m_hostedRegistration.registrationLeaseExpiresAtMs =
        refreshResult.registrationExpiresAtMs;
    m_hostedRegistration.releaseRequest.directoryRegistrationExpiresAtMs =
        refreshResult.registrationExpiresAtMs;
    m_lastHostRequest.directoryRegistrationExpiresAtMs =
        refreshResult.registrationExpiresAtMs;
  }
  return true;
}

uint64_t NetworkSessionManager::ComputeHostedSessionRefreshAtMs() const {
  if (!HasHostedSessionRegistration() ||
      m_hostedRegistration.registrationLeaseExpiresAtMs == 0) {
    return 0;
  }

  uint64_t leaseDuration = 0;
  if (m_hostedRegistration.registrationLeaseExpiresAtMs >
      m_hostedRegistration.registrationLeaseIssuedAtMs) {
    leaseDuration = m_hostedRegistration.registrationLeaseExpiresAtMs -
                    m_hostedRegistration.registrationLeaseIssuedAtMs;
  }
  uint64_t refreshLead = leaseDuration / 2;
  if (refreshLead < 1000) {
    refreshLead = 1000;
  }
  if (refreshLead > 30000) {
    refreshLead = 30000;
  }
  if (m_hostedRegistration.registrationLeaseExpiresAtMs <= refreshLead) {
    return 0;
  }
  return m_hostedRegistration.registrationLeaseExpiresAtMs - refreshLead;
}

bool NetworkSessionManager::IsTerminalHostedReleaseFailure(
    DisconnectReason reason) {
  return reason == DisconnectReason::SessionClosed ||
         reason == DisconnectReason::ProtocolError ||
         reason == DisconnectReason::VersionMismatch ||
         reason == DisconnectReason::AuthRejected;
}

bool NetworkSessionManager::IsRetryableHostedReleaseFailure(
    DisconnectReason reason) {
  return reason == DisconnectReason::Timeout ||
         reason == DisconnectReason::TransportError ||
         reason == DisconnectReason::RateLimited;
}

void NetworkSessionManager::ResetDiagnostics() {
  m_connectionStatus.bootstrapFailureReason = DisconnectReason::None;
  m_connectionStatus.bootstrapDetail.clear();
  m_connectionStatus.handshakeFailureReason = DisconnectReason::None;
  m_connectionStatus.handshakeDetail.clear();
  m_connectionStatus.activeEndpoint = NetworkEndpoint{};
  m_connectionStatus.bindEndpoint = NetworkEndpoint{};
  m_connectionStatus.advertisedEndpoint = NetworkEndpoint{};
  m_connectionStatus.resolvedJoinTarget = NetworkEndpoint{};
}

void NetworkSessionManager::SetBootstrapFailureStatus(DisconnectReason reason,
                                                      const std::string &detail) {
  m_connectionStatus.handshakeFailureReason = DisconnectReason::None;
  m_connectionStatus.handshakeDetail.clear();
  m_connectionStatus.bootstrapFailureReason = reason;
  m_connectionStatus.bootstrapDetail = detail;
  SetStatus(ConnectionState::Failed, reason, detail);
}

void NetworkSessionManager::SetHandshakeFailureStatus(DisconnectReason reason,
                                                      const std::string &detail) {
  m_connectionStatus.bootstrapFailureReason = DisconnectReason::None;
  m_connectionStatus.bootstrapDetail.clear();
  m_connectionStatus.handshakeFailureReason = reason;
  m_connectionStatus.handshakeDetail = detail;
  SetStatus(ConnectionState::Failed, reason, detail);
}

void NetworkSessionManager::RefreshEndpointDiagnostics() {
  m_connectionStatus.bindEndpoint = m_activeSession.bindEndpoint;
  m_connectionStatus.advertisedEndpoint = m_activeSession.advertisedEndpoint;
  if (m_activeSession.resolvedEndpoint.IsConfigured()) {
    m_connectionStatus.resolvedJoinTarget = m_activeSession.resolvedEndpoint;
  } else if (m_lastJoinRequest.targetEndpoint.IsConfigured()) {
    m_connectionStatus.resolvedJoinTarget = m_lastJoinRequest.targetEndpoint;
  } else {
    m_connectionStatus.resolvedJoinTarget = NetworkEndpoint{};
  }
}

void NetworkSessionManager::SetStatus(ConnectionState state,
                                      DisconnectReason reason,
                                      const std::string &detail) {
  RefreshEndpointDiagnostics();
  m_connectionStatus.state = state;
  m_connectionStatus.disconnectReason = reason;
  m_connectionStatus.detailMessage = detail;
  m_connectionStatus.sessionId = m_activeSession.sessionId;
  m_connectionStatus.isAuthenticated =
      (state == ConnectionState::Connected && !m_runtime.HasClientTransport()) ||
      m_runtime.IsSessionAuthenticated();
  if (m_connectionStatus.resolvedJoinTarget.IsConfigured()) {
    m_connectionStatus.activeEndpoint = m_connectionStatus.resolvedJoinTarget;
  } else if (m_connectionStatus.advertisedEndpoint.IsConfigured()) {
    m_connectionStatus.activeEndpoint = m_connectionStatus.advertisedEndpoint;
  } else if (m_connectionStatus.bindEndpoint.IsConfigured()) {
    m_connectionStatus.activeEndpoint = m_connectionStatus.bindEndpoint;
  } else {
    m_connectionStatus.activeEndpoint = NetworkEndpoint{};
  }
}
} // namespace ToolKit::ToolKitNetworking
