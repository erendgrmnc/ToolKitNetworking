#include "NetworkSessionManager.h"
#include <chrono>
#include <future>
#include <thread>

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

std::shared_ptr<ISessionBootstrapProvider>
ShareBootstrapProvider(SessionBootstrapProviderPtr provider) {
  return std::shared_ptr<ISessionBootstrapProvider>(provider.release());
}

template <typename TaskFn>
auto LaunchDetachedTask(TaskFn &&taskFn)
    -> std::shared_future<decltype(taskFn())> {
  using ResultT = decltype(taskFn());
  std::packaged_task<ResultT()> task(std::forward<TaskFn>(taskFn));
  auto future = task.get_future().share();
  std::thread(std::move(task)).detach();
  return future;
}

template <typename ResultT>
bool IsFutureReady(const std::shared_future<ResultT> &future) {
  return future.valid() &&
         future.wait_for(std::chrono::milliseconds(0)) ==
             std::future_status::ready;
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

  m_connectionStatus = ConnectionStatus{};
  m_connectionStatus.detailMessage = "Idle";
}

NetworkSessionManager::~NetworkSessionManager() {
  m_pendingBootstrap = PendingBootstrapState{};
  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }
  (void)ReleaseHostedSessionRegistration(true, true);
  ClearHostedSessionRegistrationState();
}

bool NetworkSessionManager::StartConfiguredSession() {
  if (!ConsumePendingReleaseIfReady()) {
    return false;
  }
  if (!ConsumePendingRefreshIfReady()) {
    return false;
  }
  if (!ConsumePendingBootstrapIfReady()) {
    return false;
  }
  if (m_pendingBootstrap.active) {
    SetBootstrapFailureStatus(
        DisconnectReason::BootstrapFailed,
        "A session directory bootstrap operation is already in progress.");
    return false;
  }

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

  std::shared_ptr<ISessionBootstrapProvider> bootstrapProvider;
  const bool usesRuntimeDirectoryService =
      !m_bootstrapProviderFactory && config.joinMethod == JoinMethod::SessionDirectory;
  if (m_bootstrapProviderFactory) {
    bootstrapProvider = ShareBootstrapProvider(
        m_bootstrapProviderFactory(config.joinMethod));
  } else if (config.joinMethod == JoinMethod::SessionDirectory) {
    const SessionDirectoryBrokerRuntimeConfig brokerConfig =
        m_runtime.GetSessionDirectoryBrokerRuntimeConfig();
    const SessionDirectoryServiceBuildResult serviceBuild =
        m_runtime.BuildSessionDirectoryService(brokerConfig);
    if (!serviceBuild.success || !serviceBuild.service) {
      SetBootstrapFailureStatus(
          serviceBuild.disconnectReason == DisconnectReason::None
              ? DisconnectReason::BootstrapFailed
              : serviceBuild.disconnectReason,
          SessionCore::SanitizeDiagnosticDetail(
              serviceBuild.detailMessage.empty()
                  ? "No session directory service is configured."
                  : serviceBuild.detailMessage));
      return false;
    }
    bootstrapProvider = ShareBootstrapProvider(
        CreateBootstrapProvider(config.joinMethod, serviceBuild.service));
  } else {
    bootstrapProvider =
        ShareBootstrapProvider(CreateBootstrapProvider(config.joinMethod));
  }
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
  }

  if (shouldJoin) {
    m_lastJoinRequest = SessionCore::BuildJoinRequest(config, overrides);
  }

  if (ShouldUseAsyncSessionDirectoryFlow(usesRuntimeDirectoryService,
                                         config.joinMethod)) {
    if (!StartAsyncSessionDirectoryBootstrap(bootstrapProvider, shouldHost,
                                             shouldJoin, hostingMode)) {
      SetBootstrapFailureStatus(
          DisconnectReason::BootstrapFailed,
          "Failed to queue session directory broker bootstrap work.");
      return false;
    }

    const String detail =
        shouldHost && shouldJoin
            ? "Registering hosted session and resolving broker join route."
        : shouldHost ? "Registering hosted session with the broker."
                     : "Resolving broker join route.";
    SetStatus(ConnectionState::Resolving, DisconnectReason::None, detail);
    return true;
  }

  if (shouldHost) {
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
      CaptureHostedSessionRegistration(bootstrapProvider, m_lastHostRequest);
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
  m_pendingBootstrap = PendingBootstrapState{};
  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }

  const HostedReleaseResult releaseResult =
      EnsureHostedSessionReleaseScheduled(true);
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
  if (!ConsumePendingReleaseIfReady()) {
    return;
  }
  if (!ConsumePendingRefreshIfReady()) {
    return;
  }
  if (!ConsumePendingBootstrapIfReady()) {
    return;
  }

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
      EnsureHostedSessionReleaseScheduled(true);

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
      EnsureHostedSessionReleaseScheduled(true);
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
    EnsureHostedSessionReleaseScheduled(true);
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
      EnsureHostedSessionReleaseScheduled(true);

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
  if (resolvedSession.resolvedRouteKind != ResolvedRouteKind::Unknown) {
    m_activeSession.resolvedRouteKind = resolvedSession.resolvedRouteKind;
  }
  if (resolvedSession.resolvedRouteExpiresAtMs != 0) {
    m_activeSession.resolvedRouteExpiresAtMs =
        resolvedSession.resolvedRouteExpiresAtMs;
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
    std::shared_ptr<ISessionBootstrapProvider> provider,
    const SessionHostRequest &request, bool asyncLifecycle) {
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
  m_hostedRegistration.asyncLifecycle = asyncLifecycle;
  m_hostedRegistration.releasePending = false;
  m_hostedRegistration.nextReleaseRetryAtMs = 0;
  m_hostedRegistration.lastReleaseFailureReason = DisconnectReason::None;
  m_hostedRegistration.lastReleaseFailureDetail.clear();
}

void NetworkSessionManager::ClearHostedSessionRegistrationState() {
  m_hostedRegistration = HostedRegistrationState{};
  m_pendingRefresh = PendingRefreshState{};
  m_pendingRelease = PendingReleaseState{};
  m_lastHostRequest.directoryRegistrationHandle.clear();
  m_lastHostRequest.directoryRegistrationExpiresAtMs = 0;
}

bool NetworkSessionManager::TryClearPendingHostedRegistrationForStart() {
  if (!HasHostedSessionRegistration()) {
    return true;
  }

  if (!ConsumePendingReleaseIfReady()) {
    return false;
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

  if (m_hostedRegistration.asyncLifecycle) {
    BeginPendingReleaseAttempt(true);
    SetBootstrapFailureStatus(
        DisconnectReason::BootstrapFailed,
        SessionCore::SanitizeDiagnosticDetail(
            "The previous hosted session registration is being released. "
            "Retry start once cleanup completes.",
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
NetworkSessionManager::EnsureHostedSessionReleaseScheduled(bool forceAttempt,
                                                          bool allowBlockedRetry) {
  HostedReleaseResult result;
  if (!HasHostedSessionRegistration()) {
    return result;
  }

  if (!m_hostedRegistration.asyncLifecycle) {
    return ReleaseHostedSessionRegistration(forceAttempt, allowBlockedRetry);
  }

  if (!ConsumePendingReleaseIfReady()) {
    return result;
  }

  if (m_hostedRegistration.releaseBlocked && !allowBlockedRetry) {
    result.disconnectReason = m_hostedRegistration.lastReleaseFailureReason;
    result.detailMessage = m_hostedRegistration.lastReleaseFailureDetail;
    return result;
  }

  if (m_pendingRelease.active) {
    result.attempted = true;
    result.retryableFailure = m_hostedRegistration.releasePending;
    if (m_hostedRegistration.releaseBlocked) {
      result.disconnectReason = m_hostedRegistration.lastReleaseFailureReason;
      result.detailMessage = m_hostedRegistration.lastReleaseFailureDetail;
    }
    return result;
  }

  BeginPendingReleaseAttempt(forceAttempt, allowBlockedRetry);
  result.attempted = true;
  return result;
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

  if (m_hostedRegistration.asyncLifecycle) {
    BeginPendingReleaseAttempt(false);
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

  if (m_hostedRegistration.asyncLifecycle) {
    BeginPendingRefreshAttempt();
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

bool NetworkSessionManager::ShouldUseAsyncSessionDirectoryFlow(
    bool usesRuntimeDirectoryService, JoinMethod joinMethod) const {
  return usesRuntimeDirectoryService && joinMethod == JoinMethod::SessionDirectory;
}

bool NetworkSessionManager::StartAsyncSessionDirectoryBootstrap(
    std::shared_ptr<ISessionBootstrapProvider> provider, bool shouldHost,
    bool shouldJoin, HostingMode hostingMode) {
  if (!provider || m_pendingBootstrap.active) {
    return false;
  }

  const SessionHostRequest hostRequest = m_lastHostRequest;
  const SessionJoinRequest joinRequest = m_lastJoinRequest;
  m_pendingBootstrap.active = true;
  m_pendingBootstrap.future = LaunchDetachedTask(
      [provider = std::move(provider), shouldHost, shouldJoin, hostingMode,
       hostRequest, joinRequest]() mutable {
        PendingBootstrapTaskResult result;
        result.shouldHost = shouldHost;
        result.shouldJoin = shouldJoin;
        result.hostingMode = hostingMode;
        result.provider = std::move(provider);
        if (result.shouldHost) {
          result.hostResult =
              result.provider->PrepareHostSession(hostRequest);
          if (!result.hostResult.success) {
            return result;
          }
        }
        if (result.shouldJoin) {
          result.joinResult = result.provider->ResolveJoinSession(joinRequest,
                                                                 hostingMode);
        }
        return result;
      });
  return true;
}

bool NetworkSessionManager::ConsumePendingBootstrapIfReady() {
  if (!m_pendingBootstrap.active || !IsFutureReady(m_pendingBootstrap.future)) {
    return true;
  }

  const PendingBootstrapTaskResult result = m_pendingBootstrap.future.get();
  m_pendingBootstrap = PendingBootstrapState{};
  return ApplyBootstrapResult(result);
}

bool NetworkSessionManager::ApplyBootstrapResult(
    const PendingBootstrapTaskResult &result) {
  ISessionBootstrapProvider *bootstrapProviderInterface = result.provider.get();

  if (result.shouldHost) {
    if (!result.hostResult.success) {
      const String detail = SessionCore::SanitizeDiagnosticDetail(
          result.hostResult.detailMessage.empty()
              ? "Failed to prepare host bootstrap session."
              : result.hostResult.detailMessage,
          CollectSecrets(result.hostResult.request, m_lastJoinRequest));
      SetBootstrapFailureStatus(result.hostResult.disconnectReason, detail);
      return false;
    }

    m_lastHostRequest = result.hostResult.request;
    ApplyResolvedSession(result.hostResult.session);
    if (!m_lastHostRequest.directoryRegistrationHandle.empty()) {
      CaptureHostedSessionRegistration(result.provider, m_lastHostRequest, true);
      bootstrapProviderInterface = m_hostedRegistration.provider.get();
    }

    const SessionValidationResult hostValidation =
        SessionCore::ValidateHostBootstrapResult(m_lastHostRequest);
    if (!hostValidation.success) {
      EnsureHostedSessionReleaseScheduled(true);
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
      EnsureHostedSessionReleaseScheduled(true);
      SetStatus(ConnectionState::Failed, DisconnectReason::TransportError,
                "Failed to start server transport.");
      return false;
    }
  }

  if (result.shouldJoin) {
    if (!result.joinResult.success) {
      if (result.shouldHost) {
        m_runtime.StopSessionTransports();
        EnsureHostedSessionReleaseScheduled(true);
      }

      const String detail = SessionCore::SanitizeDiagnosticDetail(
          result.joinResult.detailMessage.empty()
              ? "Failed to resolve join bootstrap session."
              : result.joinResult.detailMessage,
          CollectSecrets(m_lastHostRequest, result.joinResult.request));
      SetBootstrapFailureStatus(result.joinResult.disconnectReason, detail);
      return false;
    }

    m_lastJoinRequest = result.joinResult.request;
    ApplyResolvedSession(result.joinResult.session);
    const SessionValidationResult joinValidation =
        SessionCore::ValidateJoinBootstrapResult(m_lastJoinRequest, m_activeSession);
    if (!joinValidation.success) {
      if (result.shouldHost) {
        m_runtime.StopSessionTransports();
        EnsureHostedSessionReleaseScheduled(true);
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
      if (result.shouldHost) {
        m_runtime.StopSessionTransports();
        EnsureHostedSessionReleaseScheduled(true);
      }
      m_connectionAttemptStartedAtMs = 0;
      m_handshakeStartedAtMs = 0;

      SetStatus(ConnectionState::Failed, DisconnectReason::TransportError,
                "Failed to create client connection.");
      return false;
    }

    if (result.hostingMode == HostingMode::ListenServer) {
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

void NetworkSessionManager::BeginPendingReleaseAttempt(bool forceAttempt,
                                                       bool allowBlockedRetry) {
  if (!HasHostedSessionRegistration() || m_pendingRelease.active) {
    return;
  }
  if (m_hostedRegistration.releaseBlocked && !allowBlockedRetry) {
    return;
  }

  const uint64_t nowMs = GetNowMs();
  if (m_hostedRegistration.releasePending && !forceAttempt &&
      nowMs < m_hostedRegistration.nextReleaseRetryAtMs) {
    return;
  }

  const auto provider = m_hostedRegistration.provider;
  const SessionHostRequest releaseRequest = m_hostedRegistration.releaseRequest;
  m_pendingRelease.active = true;
  m_pendingRelease.future = LaunchDetachedTask(
      [provider, releaseRequest]() { return provider->ReleaseHostedSession(releaseRequest); });
}

bool NetworkSessionManager::ConsumePendingReleaseIfReady() {
  if (!m_pendingRelease.active || !IsFutureReady(m_pendingRelease.future)) {
    return true;
  }

  const HostedSessionReleaseResult releaseResult = m_pendingRelease.future.get();
  m_pendingRelease = PendingReleaseState{};

  if (!HasHostedSessionRegistration()) {
    return true;
  }

  if (releaseResult.success ||
      releaseResult.disconnectReason == DisconnectReason::SessionClosed) {
    ClearHostedSessionRegistrationState();
    return true;
  }

  const String detail = SessionCore::SanitizeDiagnosticDetail(
      releaseResult.detailMessage.empty()
          ? "Failed to release the hosted session directory registration."
          : releaseResult.detailMessage,
      CollectSecrets(m_hostedRegistration.releaseRequest, m_lastJoinRequest));

  if (IsRetryableHostedReleaseFailure(releaseResult.disconnectReason)) {
    m_hostedRegistration.releasePending = true;
    m_hostedRegistration.releaseBlocked = false;
    m_hostedRegistration.nextReleaseRetryAtMs =
        GetNowMs() + HostedRegistrationReleaseRetryMs;
    m_hostedRegistration.lastReleaseFailureReason =
        releaseResult.disconnectReason;
    m_hostedRegistration.lastReleaseFailureDetail = detail;
    if (m_connectionStatus.state == ConnectionState::Disconnected ||
        m_connectionStatus.state == ConnectionState::Failed) {
      m_connectionStatus.detailMessage =
          AppendWarningMessage(m_connectionStatus.detailMessage, detail);
    }
    return true;
  }

  m_hostedRegistration.releaseBlocked = true;
  m_hostedRegistration.releasePending = false;
  m_hostedRegistration.nextReleaseRetryAtMs = 0;
  m_hostedRegistration.lastReleaseFailureReason =
      IsTerminalHostedReleaseFailure(releaseResult.disconnectReason)
          ? releaseResult.disconnectReason
          : DisconnectReason::ProtocolError;
  m_hostedRegistration.lastReleaseFailureDetail = detail;
  if (m_connectionStatus.state == ConnectionState::Disconnected ||
      m_connectionStatus.state == ConnectionState::Failed) {
    m_connectionStatus.detailMessage =
        AppendWarningMessage(m_connectionStatus.detailMessage, detail);
  }
  return true;
}

void NetworkSessionManager::BeginPendingRefreshAttempt() {
  if (!HasHostedSessionRegistration() || m_pendingRefresh.active ||
      m_hostedRegistration.releasePending ||
      m_hostedRegistration.registrationLeaseExpiresAtMs == 0) {
    return;
  }

  const auto provider = m_hostedRegistration.provider;
  const SessionHostRequest releaseRequest = m_hostedRegistration.releaseRequest;
  m_pendingRefresh.active = true;
  m_pendingRefresh.future = LaunchDetachedTask(
      [provider, releaseRequest]() { return provider->RefreshHostedSession(releaseRequest); });
}

bool NetworkSessionManager::ConsumePendingRefreshIfReady() {
  if (!m_pendingRefresh.active || !IsFutureReady(m_pendingRefresh.future)) {
    return true;
  }

  const HostedSessionRefreshResult refreshResult = m_pendingRefresh.future.get();
  m_pendingRefresh = PendingRefreshState{};
  if (!HasHostedSessionRegistration()) {
    return true;
  }

  if (!refreshResult.success) {
    const SessionHostRequest releaseRequest = m_hostedRegistration.releaseRequest;
    if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
      m_runtime.StopSessionTransports();
    }
    EnsureHostedSessionReleaseScheduled(true);
    m_connectionAttemptStartedAtMs = 0;
    m_handshakeStartedAtMs = 0;
    SetBootstrapFailureStatus(
        refreshResult.disconnectReason,
        SessionCore::SanitizeDiagnosticDetail(
            refreshResult.detailMessage.empty()
                ? "Failed to refresh hosted session directory registration."
                : refreshResult.detailMessage,
            CollectSecrets(releaseRequest, m_lastJoinRequest)));
    return false;
  }

  if (refreshResult.registrationExpiresAtMs != 0) {
    const uint64_t nowMs = GetNowMs();
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
