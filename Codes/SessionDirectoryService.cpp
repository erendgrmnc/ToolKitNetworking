#include "SessionDirectoryService.h"
#include "NetworkSessionCore.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace ToolKit::ToolKitNetworking {
namespace {
std::vector<String> CollectDirectorySecrets(
    const SessionDirectoryRegistrationRequest &request,
    const SessionDirectoryBrokerRegisterResponse *response = nullptr) {
  std::vector<String> secrets;
  if (!request.requestedJoinCredential.empty()) {
    secrets.push_back(request.requestedJoinCredential);
  }
  if (response != nullptr) {
    if (!response->registrationHandle.empty()) {
      secrets.push_back(response->registrationHandle);
    }
    if (!response->joinCredential.empty()) {
      secrets.push_back(response->joinCredential);
    }
  }
  return secrets;
}

std::vector<String> CollectDirectorySecrets(
    const SessionDirectoryLookupRequest &,
    const SessionDirectoryBrokerLookupResponse *response = nullptr) {
  std::vector<String> secrets;
  if (response != nullptr && !response->joinCredential.empty()) {
    secrets.push_back(response->joinCredential);
  }
  return secrets;
}

std::vector<String> CollectDirectorySecrets(
    const SessionDirectoryRefreshRequest &request) {
  if (request.registrationHandle.empty()) {
    return {};
  }
  return {request.registrationHandle};
}

std::vector<String> CollectDirectorySecrets(
    const SessionDirectoryUnregisterRequest &request) {
  if (request.registrationHandle.empty()) {
    return {};
  }
  return {request.registrationHandle};
}

struct SessionDirectoryRecord {
  String registrationHandle;
  SessionDescriptor session;
  NetworkEndpoint resolvedJoinRoute;
  ResolvedRouteKind resolvedRouteKind = ResolvedRouteKind::Unknown;
  uint64_t resolvedRouteExpiresAtMs = 0;
  String joinCredential;
  String directoryProviderName;
  uint64_t registrationExpiresAtMs = 0;
  uint64_t joinCredentialExpiresAtMs = 0;
};

uint64_t NowMs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool FillSecureRandomBytes(unsigned char *buffer, size_t size) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(size),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
  std::random_device randomDevice;
  for (size_t idx = 0; idx < size; ++idx) {
    buffer[idx] = static_cast<unsigned char>(randomDevice() & 0xFF);
  }
  return true;
#endif
}

String GenerateOpaqueValue(const char *prefix) {
  static const char *hexDigits = "0123456789abcdef";
  static std::atomic<uint64_t> fallbackCounter{0};

  String value(prefix);
  unsigned char randomBytes[16] = {};
  if (!FillSecureRandomBytes(randomBytes, sizeof(randomBytes))) {
    return String(prefix) + std::to_string(NowMs()) + "-" +
           std::to_string(fallbackCounter.fetch_add(1, std::memory_order_relaxed));
  }

  for (unsigned char byte : randomBytes) {
    value += hexDigits[(byte >> 4) & 0x0F];
    value += hexDigits[byte & 0x0F];
  }

  return value;
}

constexpr uint64_t ProcessLocalCredentialTtlMs = 10ULL * 60ULL * 1000ULL;

DisconnectReason
MapBrokerError(SessionDirectoryBrokerError errorCode) {
  switch (errorCode) {
  case SessionDirectoryBrokerError::None:
    return DisconnectReason::BootstrapFailed;
  case SessionDirectoryBrokerError::InvalidRequest:
  case SessionDirectoryBrokerError::SessionAlreadyExists:
    return DisconnectReason::BootstrapFailed;
  case SessionDirectoryBrokerError::SessionNotFound:
    return DisconnectReason::SessionClosed;
  case SessionDirectoryBrokerError::VersionMismatch:
    return DisconnectReason::VersionMismatch;
  case SessionDirectoryBrokerError::Unauthorized:
    return DisconnectReason::AuthRejected;
  case SessionDirectoryBrokerError::RateLimited:
    return DisconnectReason::RateLimited;
  case SessionDirectoryBrokerError::Timeout:
    return DisconnectReason::Timeout;
  case SessionDirectoryBrokerError::ServiceUnavailable:
    return DisconnectReason::TransportError;
  case SessionDirectoryBrokerError::ProtocolError:
    return DisconnectReason::ProtocolError;
  default:
    return DisconnectReason::BootstrapFailed;
  }
}

String DefaultBrokerErrorDetail(SessionDirectoryBrokerError errorCode) {
  switch (errorCode) {
  case SessionDirectoryBrokerError::InvalidRequest:
    return "Broker rejected the session directory request as invalid.";
  case SessionDirectoryBrokerError::SessionNotFound:
    return "Requested session identifier could not be resolved by the broker.";
  case SessionDirectoryBrokerError::SessionAlreadyExists:
    return "Requested session identifier is already registered with the broker.";
  case SessionDirectoryBrokerError::VersionMismatch:
    return "Requested session build compatibility identifier is not "
           "compatible with the hosted session.";
  case SessionDirectoryBrokerError::Unauthorized:
    return "Broker rejected the session directory request as unauthorized.";
  case SessionDirectoryBrokerError::RateLimited:
    return "Broker temporarily rate limited the session directory request.";
  case SessionDirectoryBrokerError::Timeout:
    return "Timed out while waiting for the session directory broker.";
  case SessionDirectoryBrokerError::ServiceUnavailable:
    return "Session directory broker is unavailable.";
  case SessionDirectoryBrokerError::ProtocolError:
    return "Session directory broker returned an invalid response.";
  case SessionDirectoryBrokerError::None:
  default:
    return "Session directory broker request failed.";
  }
}

class ProcessLocalSessionDirectoryService : public ISessionDirectoryService {
public:
  explicit ProcessLocalSessionDirectoryService(std::function<uint64_t()> nowProvider)
      : m_nowProvider(nowProvider ? std::move(nowProvider) : NowMs) {}

  SessionDirectoryRegistrationResult RegisterHostedSession(
      const SessionDirectoryRegistrationRequest &request) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionDirectoryRegistrationResult result;

    if (request.bindEndpoint.port == 0) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory host bootstrap requires a listen port.";
      return result;
    }

    NetworkEndpoint joinRoute = request.advertisedEndpoint;
    if (!joinRoute.IsConfigured()) {
      joinRoute = request.bindEndpoint;
    }

    if (!joinRoute.IsConfigured() || joinRoute.host == "0.0.0.0") {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory host bootstrap requires a joinable advertised "
          "endpoint.";
      return result;
    }

    const String sessionId = request.requestedSessionId.empty()
                                 ? GenerateOpaqueValue("directory-session-")
                                 : request.requestedSessionId;
    if (m_sessions.find(sessionId) != m_sessions.end()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Requested session identifier is already registered in the session "
          "directory.";
      return result;
    }

    result.success = true;
    result.directoryProviderName = "ProcessLocalSessionDirectory";
    result.registrationHandle = GenerateOpaqueValue("directory-registration-");
    result.registrationExpiresAtMs = m_nowProvider() + ProcessLocalCredentialTtlMs;
    result.joinCredentialExpiresAtMs = result.registrationExpiresAtMs;
    result.resolvedRouteExpiresAtMs = result.registrationExpiresAtMs;
    result.joinCredential = request.requestedJoinCredential.empty()
                                ? GenerateOpaqueValue("directory-key-")
                                : request.requestedJoinCredential;
    result.session.sessionId = sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::SessionDirectory;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.bindEndpoint.usage = EndpointUsage::Bind;
    result.session.advertisedEndpoint = joinRoute;
    result.session.advertisedEndpoint.usage = EndpointUsage::Advertised;
    result.session.resolvedEndpoint = joinRoute;
    result.session.resolvedEndpoint.usage = EndpointUsage::ResolvedTransport;
    result.session.resolvedRouteKind = ResolvedRouteKind::Direct;
    result.session.resolvedRouteExpiresAtMs = result.resolvedRouteExpiresAtMs;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = request.requireJoinCredential;
    result.resolvedJoinRoute = result.session.resolvedEndpoint;
    result.resolvedRouteKind = result.session.resolvedRouteKind;

    SessionDirectoryRecord record;
    record.registrationHandle = result.registrationHandle;
    record.session = result.session;
    record.resolvedJoinRoute = result.resolvedJoinRoute;
    record.resolvedRouteKind = result.resolvedRouteKind;
    record.resolvedRouteExpiresAtMs = result.resolvedRouteExpiresAtMs;
    record.joinCredential = result.joinCredential;
    record.directoryProviderName = result.directoryProviderName;
    record.registrationExpiresAtMs = result.registrationExpiresAtMs;
    record.joinCredentialExpiresAtMs = result.joinCredentialExpiresAtMs;
    m_sessions[result.session.sessionId] = record;
    m_registrationHandles[result.registrationHandle] = result.session.sessionId;
    return result;
  }

  SessionDirectoryLookupResult
  LookupSession(const SessionDirectoryLookupRequest &request) const override {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionDirectoryLookupResult result;

    if (request.sessionId.empty()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory join bootstrap requires a public session "
          "identifier.";
      return result;
    }

    auto it = m_sessions.find(request.sessionId);
    if (it == m_sessions.end()) {
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session identifier could not be resolved from the "
          "session directory.";
      return result;
    }

    if ((it->second.registrationExpiresAtMs != 0 &&
         it->second.registrationExpiresAtMs <= m_nowProvider()) ||
        (it->second.joinCredentialExpiresAtMs != 0 &&
         it->second.joinCredentialExpiresAtMs <= m_nowProvider())) {
      const String expiredRegistrationHandle = it->second.registrationHandle;
      m_sessions.erase(it);
      if (!expiredRegistrationHandle.empty()) {
        m_registrationHandles.erase(expiredRegistrationHandle);
      }
      result.disconnectReason = DisconnectReason::AuthRejected;
      result.detailMessage =
          "Requested session join credential has expired in the session "
          "directory.";
      return result;
    }

    if (!request.buildCompatibilityId.empty() &&
        !it->second.session.buildCompatibilityId.empty() &&
        request.buildCompatibilityId !=
            it->second.session.buildCompatibilityId) {
      result.disconnectReason = DisconnectReason::VersionMismatch;
      result.detailMessage =
          "Requested session build compatibility identifier is not "
          "compatible with the hosted session.";
      return result;
    }

    result.success = true;
    result.session = it->second.session;
    result.resolvedJoinRoute = it->second.resolvedJoinRoute;
    result.resolvedRouteKind = it->second.resolvedRouteKind;
    result.resolvedRouteExpiresAtMs = it->second.resolvedRouteExpiresAtMs;
    result.joinCredential = it->second.joinCredential;
    result.directoryProviderName = it->second.directoryProviderName;
    result.joinCredentialExpiresAtMs = it->second.joinCredentialExpiresAtMs;
    return result;
  }

  SessionDirectoryRefreshResult
  RefreshHostedSession(const SessionDirectoryRefreshRequest &request) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionDirectoryRefreshResult result;
    if (request.registrationHandle.empty()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory refresh requires a registration handle.";
      return result;
    }

    auto handleIt = m_registrationHandles.find(request.registrationHandle);
    if (handleIt == m_registrationHandles.end()) {
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session registration handle could not be resolved from "
          "the session directory.";
      return result;
    }

    auto sessionIt = m_sessions.find(handleIt->second);
    if (sessionIt == m_sessions.end()) {
      m_registrationHandles.erase(handleIt);
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session registration handle could not be resolved from "
          "the session directory.";
      return result;
    }

    if (sessionIt->second.registrationExpiresAtMs != 0 &&
        sessionIt->second.registrationExpiresAtMs <= m_nowProvider()) {
      m_sessions.erase(sessionIt);
      m_registrationHandles.erase(handleIt);
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session registration handle has expired in the session "
          "directory.";
      return result;
    }

    sessionIt->second.registrationExpiresAtMs =
        m_nowProvider() + ProcessLocalCredentialTtlMs;
    sessionIt->second.joinCredentialExpiresAtMs =
        sessionIt->second.registrationExpiresAtMs;
    sessionIt->second.resolvedRouteExpiresAtMs =
        sessionIt->second.registrationExpiresAtMs;
    sessionIt->second.session.resolvedRouteExpiresAtMs =
        sessionIt->second.registrationExpiresAtMs;
    result.success = true;
    result.registrationExpiresAtMs = sessionIt->second.registrationExpiresAtMs;
    return result;
  }

  SessionDirectoryUnregisterResult UnregisterHostedSession(
      const SessionDirectoryUnregisterRequest &request) override {
    std::lock_guard<std::mutex> lock(m_mutex);
    SessionDirectoryUnregisterResult result;
    if (request.registrationHandle.empty()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory unregister requires a registration handle.";
      return result;
    }

    auto handleIt = m_registrationHandles.find(request.registrationHandle);
    if (handleIt == m_registrationHandles.end()) {
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session registration handle could not be resolved from "
          "the session directory.";
      return result;
    }

    m_sessions.erase(handleIt->second);
    m_registrationHandles.erase(handleIt);
    result.success = true;
    return result;
  }

private:
  std::function<uint64_t()> m_nowProvider;
  mutable std::mutex m_mutex;
  mutable std::map<String, SessionDirectoryRecord> m_sessions;
  mutable std::map<String, String> m_registrationHandles;
};

class BrokerBackedSessionDirectoryService : public ISessionDirectoryService {
public:
  explicit BrokerBackedSessionDirectoryService(
      SessionDirectoryBrokerClientPtr brokerClient)
      : m_brokerClient(std::move(brokerClient)) {}

  SessionDirectoryRegistrationResult RegisterHostedSession(
      const SessionDirectoryRegistrationRequest &request) override {
    SessionDirectoryRegistrationResult result;
    if (!m_brokerClient) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "No session directory broker client is configured.";
      return result;
    }

    SessionDirectoryBrokerRegisterRequest brokerRequest;
    brokerRequest.hostingMode = request.hostingMode;
    brokerRequest.requestedSessionId = request.requestedSessionId;
    brokerRequest.requestedJoinCredential = request.requestedJoinCredential;
    brokerRequest.bindEndpoint = request.bindEndpoint;
    brokerRequest.advertisedEndpoint = request.advertisedEndpoint;
    brokerRequest.buildCompatibilityId = request.buildCompatibilityId;
    brokerRequest.requireJoinCredential = request.requireJoinCredential;

    const SessionDirectoryBrokerRegisterResponse brokerResponse =
        m_brokerClient->RegisterSession(brokerRequest);
    if (!brokerResponse.success) {
      result.disconnectReason = MapBrokerError(brokerResponse.errorCode);
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(
              brokerResponse.detailMessage.empty()
                  ? DefaultBrokerErrorDetail(brokerResponse.errorCode)
                  : brokerResponse.detailMessage,
              CollectDirectorySecrets(request, &brokerResponse));
      return result;
    }

    result.success = true;
    result.registrationHandle = brokerResponse.registrationHandle;
    result.registrationExpiresAtMs = brokerResponse.registrationExpiresAtMs;
    result.session = brokerResponse.session;
    result.resolvedJoinRoute = brokerResponse.resolvedJoinRoute;
    result.resolvedRouteKind = brokerResponse.resolvedRouteKind;
    result.resolvedRouteExpiresAtMs = brokerResponse.resolvedRouteExpiresAtMs;
    result.joinCredential = brokerResponse.joinCredential;
    result.directoryProviderName = brokerResponse.providerName;
    result.joinCredentialExpiresAtMs =
        brokerResponse.joinCredentialExpiresAtMs;
    return result;
  }

  SessionDirectoryLookupResult
  LookupSession(const SessionDirectoryLookupRequest &request) const override {
    SessionDirectoryLookupResult result;
    if (!m_brokerClient) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "No session directory broker client is configured.";
      return result;
    }

    SessionDirectoryBrokerLookupRequest brokerRequest;
    brokerRequest.sessionId = request.sessionId;
    brokerRequest.buildCompatibilityId = request.buildCompatibilityId;

    const SessionDirectoryBrokerLookupResponse brokerResponse =
        m_brokerClient->LookupSession(brokerRequest);
    if (!brokerResponse.success) {
      result.disconnectReason = MapBrokerError(brokerResponse.errorCode);
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(
              brokerResponse.detailMessage.empty()
                  ? DefaultBrokerErrorDetail(brokerResponse.errorCode)
                  : brokerResponse.detailMessage,
              CollectDirectorySecrets(request, &brokerResponse));
      return result;
    }

    result.success = true;
    result.session = brokerResponse.session;
    result.resolvedJoinRoute = brokerResponse.resolvedJoinRoute;
    result.resolvedRouteKind = brokerResponse.resolvedRouteKind;
    result.resolvedRouteExpiresAtMs = brokerResponse.resolvedRouteExpiresAtMs;
    result.joinCredential = brokerResponse.joinCredential;
    result.directoryProviderName = brokerResponse.providerName;
    result.joinCredentialExpiresAtMs =
        brokerResponse.joinCredentialExpiresAtMs;
    return result;
  }

  SessionDirectoryRefreshResult
  RefreshHostedSession(const SessionDirectoryRefreshRequest &request) override {
    SessionDirectoryRefreshResult result;
    if (!m_brokerClient) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "No session directory broker client is configured.";
      return result;
    }

    SessionDirectoryBrokerRefreshRequest brokerRequest;
    brokerRequest.registrationHandle = request.registrationHandle;

    const SessionDirectoryBrokerRefreshResponse brokerResponse =
        m_brokerClient->RefreshSessionRegistration(brokerRequest);
    if (!brokerResponse.success) {
      result.disconnectReason = MapBrokerError(brokerResponse.errorCode);
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(
              brokerResponse.detailMessage.empty()
                  ? DefaultBrokerErrorDetail(brokerResponse.errorCode)
                  : brokerResponse.detailMessage,
              CollectDirectorySecrets(request));
      return result;
    }

    result.success = true;
    result.registrationExpiresAtMs = brokerResponse.registrationExpiresAtMs;
    return result;
  }

  SessionDirectoryUnregisterResult UnregisterHostedSession(
      const SessionDirectoryUnregisterRequest &request) override {
    SessionDirectoryUnregisterResult result;
    if (!m_brokerClient) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "No session directory broker client is configured.";
      return result;
    }

    SessionDirectoryBrokerUnregisterRequest brokerRequest;
    brokerRequest.registrationHandle = request.registrationHandle;

    const SessionDirectoryBrokerUnregisterResponse brokerResponse =
        m_brokerClient->UnregisterSession(brokerRequest);
    if (!brokerResponse.success) {
      result.disconnectReason = MapBrokerError(brokerResponse.errorCode);
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(
              brokerResponse.detailMessage.empty()
                  ? DefaultBrokerErrorDetail(brokerResponse.errorCode)
                  : brokerResponse.detailMessage,
              CollectDirectorySecrets(request));
      return result;
    }

    result.success = true;
    return result;
  }

private:
  SessionDirectoryBrokerClientPtr m_brokerClient;
};
} // namespace

SessionDirectoryServicePtr CreateDefaultSessionDirectoryService() {
  return CreateProcessLocalSessionDirectoryService();
}

SessionDirectoryServicePtr CreateProcessLocalSessionDirectoryService(
    std::function<uint64_t()> nowProvider) {
  return std::make_shared<ProcessLocalSessionDirectoryService>(
      std::move(nowProvider));
}

SessionDirectoryServicePtr CreateSharedProcessLocalSessionDirectoryService() {
  // This shared process-local directory preserves zero-config local host/join
  // interop for the fake SessionDirectory path. Production broker-backed flows
  // should inject an explicit service instead of relying on shared local state.
  static SessionDirectoryServicePtr service =
      CreateProcessLocalSessionDirectoryService();
  return service;
}

SessionDirectoryServicePtr
CreateBrokerBackedSessionDirectoryService(
    SessionDirectoryBrokerClientPtr brokerClient) {
  return std::make_shared<BrokerBackedSessionDirectoryService>(
      std::move(brokerClient));
}
} // namespace ToolKit::ToolKitNetworking
