#include "NetworkSessionManager.h"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ToolKit::ToolKitNetworking {
namespace {
CommandLineSessionOverrides ReadProcessCommandLineOverrides() {
#ifdef _WIN32
  const char *commandLine = GetCommandLineA();
  if (commandLine != nullptr) {
    return SessionCore::ParseCommandLineOverrides(commandLine);
  }
#endif
  return CommandLineSessionOverrides{};
}
} // namespace

NetworkSessionManager::NetworkSessionManager(
    INetworkSessionRuntime &runtime,
    CommandLineOverridesProvider commandLineOverridesProvider,
    ClockNowProvider clockNowProvider)
    : m_runtime(runtime),
      m_commandLineOverridesProvider(commandLineOverridesProvider),
      m_clockNowProvider(clockNowProvider) {
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

  m_connectionStatus.state = ConnectionState::Idle;
  m_connectionStatus.disconnectReason = DisconnectReason::None;
  m_connectionStatus.detailMessage = "Idle";
}

bool NetworkSessionManager::StartConfiguredSession() {
  const CommandLineSessionOverrides overrides =
      m_commandLineOverridesProvider();
  const HostingMode hostingMode =
      overrides.hasHostingModeOverride ? overrides.hostingMode
                                       : ResolveConfiguredHostingMode();

  m_lastHostRequest = SessionHostRequest{};
  m_lastJoinRequest = SessionJoinRequest{};
  m_activeSession = SessionDescriptor{};
  m_activeSession.hostingMode = hostingMode;
  m_activeSession.sessionId = m_lastHostRequest.sessionId;

  const bool shouldHost = hostingMode == HostingMode::DedicatedServer ||
                          hostingMode == HostingMode::ListenServer;
  const bool shouldJoin = hostingMode == HostingMode::Client ||
                          hostingMode == HostingMode::ListenServer;

  if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
    m_runtime.StopSessionTransports();
  }

  if (!shouldHost && !shouldJoin) {
    m_connectionAttemptStartedAtMs = 0;

    SetStatus(ConnectionState::Idle, DisconnectReason::None,
              "Networking disabled for current hosting mode.");
    return true;
  }

  if (shouldHost) {
    SessionBootstrapConfig config;
    config.hostingMode = hostingMode;
    m_lastHostRequest = SessionCore::BuildHostRequest(config, overrides);
    m_activeSession.sessionId = m_lastHostRequest.sessionId;
    m_activeSession.bindEndpoint = m_lastHostRequest.bindEndpoint;
    m_activeSession.advertisedEndpoint = m_lastHostRequest.advertisedEndpoint;
    m_activeSession.buildCompatibilityId = m_lastHostRequest.buildCompatibilityId;
    m_activeSession.isJoinCredentialRequired =
        m_lastHostRequest.requireJoinCredential;

    SetStatus(ConnectionState::StartingHost);
    if (!m_runtime.StartServerTransport(m_lastHostRequest.bindEndpoint.port)) {
      SetStatus(ConnectionState::Failed, DisconnectReason::TransportError,
                "Failed to start server transport.");
      return false;
    }
  }

  if (shouldJoin) {
    SessionBootstrapConfig config;
    config.hostingMode = hostingMode;
    m_lastJoinRequest = SessionCore::BuildJoinRequest(config, overrides);
    m_activeSession.joinMethod = m_lastJoinRequest.joinMethod;
    m_activeSession.resolvedEndpoint = m_lastJoinRequest.targetEndpoint;
    m_activeSession.sessionId = m_lastJoinRequest.sessionId;
    m_activeSession.buildCompatibilityId = m_lastJoinRequest.buildCompatibilityId;
    m_activeSession.isJoinCredentialRequired =
        !m_lastJoinRequest.joinCredential.empty();
    m_connectionAttemptStartedAtMs = GetNowMs();
    m_handshakeStartedAtMs = 0;

    SetStatus(ConnectionState::Connecting);
    if (!m_runtime.StartClientTransport(m_lastJoinRequest.targetEndpoint.host,
                                        m_lastJoinRequest.targetEndpoint.port)) {
      if (shouldHost) {
        m_runtime.StopSessionTransports();
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

  SetStatus(ConnectionState::Disconnected, reason,
            reason == DisconnectReason::UserRequested
                ? "Session stopped by user request."
                : "Session stopped.");

  m_connectionAttemptStartedAtMs = 0;
  m_handshakeStartedAtMs = 0;
  m_activeSession = SessionDescriptor{};
  m_lastHostRequest = SessionHostRequest{};
  m_lastJoinRequest = SessionJoinRequest{};
}

void NetworkSessionManager::Update() {
  if (m_connectionStatus.state == ConnectionState::Connecting &&
      m_lastJoinRequest.connectionTimeoutMs > 0) {
    const uint64_t elapsedMs = GetNowMs() - m_connectionAttemptStartedAtMs;
    if (elapsedMs >= m_lastJoinRequest.connectionTimeoutMs) {
      if (m_runtime.HasServerTransport() || m_runtime.HasClientTransport()) {
        m_runtime.StopSessionTransports();
      }

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
      m_connectionAttemptStartedAtMs = 0;
      m_handshakeStartedAtMs = 0;
      SetStatus(ConnectionState::Failed,
                m_runtime.GetSessionAuthFailureReason(),
                m_runtime.GetSessionAuthFailureDetail().empty()
                    ? "Failed to start session handshake."
                    : m_runtime.GetSessionAuthFailureDetail());
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
    m_handshakeStartedAtMs = 0;
    SetStatus(ConnectionState::Failed, m_runtime.GetSessionAuthFailureReason(),
              m_runtime.GetSessionAuthFailureDetail().empty()
                  ? "Session handshake rejected."
                  : m_runtime.GetSessionAuthFailureDetail());
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

      m_handshakeStartedAtMs = 0;
      SetStatus(ConnectionState::Failed, DisconnectReason::Timeout,
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
  return m_runtime.GetConfiguredHostingMode();
}

uint64_t NetworkSessionManager::GetNowMs() const { return m_clockNowProvider(); }

void NetworkSessionManager::SetStatus(ConnectionState state,
                                      DisconnectReason reason,
                                      const std::string &detail) {
  m_connectionStatus.state = state;
  m_connectionStatus.disconnectReason = reason;
  m_connectionStatus.detailMessage = detail;
  m_connectionStatus.sessionId = m_activeSession.sessionId;
  m_connectionStatus.isAuthenticated =
      (state == ConnectionState::Connected && !m_runtime.HasClientTransport()) ||
      m_runtime.IsSessionAuthenticated();
  if (state == ConnectionState::StartingHost || state == ConnectionState::Connected) {
    if (m_activeSession.resolvedEndpoint.IsConfigured()) {
      m_connectionStatus.activeEndpoint = m_activeSession.resolvedEndpoint;
    } else if (m_activeSession.advertisedEndpoint.IsConfigured()) {
      m_connectionStatus.activeEndpoint = m_activeSession.advertisedEndpoint;
    } else {
      m_connectionStatus.activeEndpoint = m_activeSession.bindEndpoint;
    }
  } else if (state == ConnectionState::Connecting) {
    m_connectionStatus.activeEndpoint = m_lastJoinRequest.targetEndpoint;
  } else {
    m_connectionStatus.activeEndpoint = NetworkEndpoint{};
  }
}
} // namespace ToolKit::ToolKitNetworking
