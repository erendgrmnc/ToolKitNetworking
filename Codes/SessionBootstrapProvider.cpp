#include "SessionBootstrapProvider.h"
#include "NetworkSessionCore.h"
#include "SessionDirectoryService.h"

namespace ToolKit::ToolKitNetworking {
namespace {
const char *JoinMethodName(JoinMethod joinMethod) {
  switch (joinMethod) {
  case JoinMethod::DirectAddress:
    return "direct address";
  case JoinMethod::SessionDirectory:
    return "session directory";
  case JoinMethod::LanDiscovery:
    return "LAN discovery";
  case JoinMethod::BrokeredHostedSession:
    return "brokered hosted session";
  default:
    return "unknown";
  }
}

class SessionDirectoryBootstrapProvider : public ISessionBootstrapProvider {
public:
  explicit SessionDirectoryBootstrapProvider(
      SessionDirectoryServicePtr directoryService)
      : m_directoryService(directoryService
                               ? directoryService
                               // Zero-config SessionDirectory uses a shared
                               // process-local fake directory so local host and
                               // join managers can interoperate without explicit
                               // injection. Production paths should inject a
                               // real broker-backed service explicitly.
                               : CreateSharedProcessLocalSessionDirectoryService()) {}

  JoinMethod GetJoinMethod() const override {
    return JoinMethod::SessionDirectory;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.request = request;

    SessionDirectoryRegistrationRequest directoryRequest;
    directoryRequest.hostingMode = request.hostingMode;
    directoryRequest.requestedSessionId = request.sessionId;
    directoryRequest.requestedJoinCredential = request.joinCredential;
    directoryRequest.bindEndpoint = request.bindEndpoint;
    directoryRequest.advertisedEndpoint = request.advertisedEndpoint;
    directoryRequest.buildCompatibilityId = request.buildCompatibilityId;
    directoryRequest.requireJoinCredential = request.requireJoinCredential;

    const SessionDirectoryRegistrationResult directoryResult =
        m_directoryService->RegisterHostedSession(directoryRequest);
    if (!directoryResult.success) {
      result.disconnectReason = directoryResult.disconnectReason;
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(directoryResult.detailMessage);
      return result;
    }

    result.success = true;
    result.request.sessionId = directoryResult.session.sessionId;
    result.request.joinCredential = directoryResult.joinCredential;
    result.request.directoryRegistrationHandle =
        directoryResult.registrationHandle;
    result.request.directoryRegistrationExpiresAtMs =
        directoryResult.registrationExpiresAtMs;
    result.request.requireJoinCredential =
        directoryResult.session.isJoinCredentialRequired ||
        !directoryResult.joinCredential.empty();
    result.session = directoryResult.session;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.request = request;

    SessionDirectoryLookupRequest directoryRequest;
    directoryRequest.sessionId = request.sessionId;
    directoryRequest.buildCompatibilityId = request.buildCompatibilityId;

    const SessionDirectoryLookupResult directoryResult =
        m_directoryService->LookupSession(directoryRequest);
    if (!directoryResult.success) {
      result.disconnectReason = directoryResult.disconnectReason;
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(directoryResult.detailMessage);
      return result;
    }

    result.success = true;
    result.request.sessionId = directoryResult.session.sessionId;
    result.request.joinCredential = directoryResult.joinCredential;
    result.request.targetEndpoint = directoryResult.resolvedJoinRoute;
    result.session = directoryResult.session;
    result.session.resolvedEndpoint = directoryResult.resolvedJoinRoute;
    result.session.resolvedEndpoint.usage = EndpointUsage::ResolvedTransport;
    result.session.isJoinCredentialRequired =
        result.session.isJoinCredentialRequired ||
        !directoryResult.joinCredential.empty();
    return result;
  }

  HostedSessionRefreshResult RefreshHostedSession(
      const SessionHostRequest &request) override {
    HostedSessionRefreshResult result;
    if (request.directoryRegistrationHandle.empty()) {
      return result;
    }

    const SessionDirectoryRefreshResult directoryResult =
        m_directoryService->RefreshHostedSession(
            SessionDirectoryRefreshRequest{request.directoryRegistrationHandle});
    if (!directoryResult.success) {
      result.success = false;
      result.disconnectReason = directoryResult.disconnectReason;
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(directoryResult.detailMessage);
      return result;
    }

    result.registrationExpiresAtMs = directoryResult.registrationExpiresAtMs;
    return result;
  }

  HostedSessionReleaseResult ReleaseHostedSession(
      const SessionHostRequest &request) override {
    HostedSessionReleaseResult result;
    if (request.directoryRegistrationHandle.empty()) {
      return result;
    }

    const SessionDirectoryUnregisterResult directoryResult =
        m_directoryService->UnregisterHostedSession(SessionDirectoryUnregisterRequest{
            request.directoryRegistrationHandle});
    if (!directoryResult.success) {
      result.success = false;
      result.disconnectReason = directoryResult.disconnectReason;
      result.detailMessage =
          SessionCore::SanitizeDiagnosticDetail(directoryResult.detailMessage);
    }
    return result;
  }

private:
  SessionDirectoryServicePtr m_directoryService;
};

class DirectAddressBootstrapProvider : public ISessionBootstrapProvider {
public:
  JoinMethod GetJoinMethod() const override { return JoinMethod::DirectAddress; }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.request = request;

    if (request.bindEndpoint.port == 0) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage = "Direct host bootstrap requires a listen port.";
      return result;
    }

    if (request.requireJoinCredential && request.joinCredential.empty()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Direct host bootstrap requires a join credential when credential "
          "enforcement is enabled.";
      return result;
    }

    result.success = true;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::DirectAddress;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = request.advertisedEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = request.requireJoinCredential;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.request = request;

    if (!request.targetEndpoint.IsConfigured()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Direct join bootstrap requires an explicit join target endpoint.";
      return result;
    }

    result.success = true;
    result.session.sessionId = request.sessionId;
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = request.joinMethod;
    result.session.resolvedEndpoint = request.targetEndpoint;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = !request.joinCredential.empty();
    return result;
  }
};

class UnsupportedBootstrapProvider : public ISessionBootstrapProvider {
public:
  explicit UnsupportedBootstrapProvider(JoinMethod joinMethod)
      : m_joinMethod(joinMethod) {}

  JoinMethod GetJoinMethod() const override { return m_joinMethod; }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.request = request;
    result.disconnectReason = DisconnectReason::BootstrapFailed;
    result.detailMessage = String("Bootstrap provider for ") +
                           JoinMethodName(m_joinMethod) +
                           " is not implemented yet.";
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode) const override {
    BootstrapJoinResult result;
    result.request = request;
    result.disconnectReason = DisconnectReason::BootstrapFailed;
    result.detailMessage = String("Bootstrap provider for ") +
                           JoinMethodName(m_joinMethod) +
                           " is not implemented yet.";
    return result;
  }

private:
  JoinMethod m_joinMethod;
};
} // namespace

SessionBootstrapProviderPtr CreateBootstrapProvider(JoinMethod joinMethod) {
  return CreateBootstrapProvider(joinMethod, {});
}

SessionBootstrapProviderPtr
CreateBootstrapProvider(JoinMethod joinMethod,
                        SessionDirectoryServicePtr directoryService) {
  switch (joinMethod) {
  case JoinMethod::DirectAddress:
    return std::make_unique<DirectAddressBootstrapProvider>();
  case JoinMethod::SessionDirectory:
    return std::make_unique<SessionDirectoryBootstrapProvider>(
        std::move(directoryService));
  case JoinMethod::LanDiscovery:
  case JoinMethod::BrokeredHostedSession:
    return std::make_unique<UnsupportedBootstrapProvider>(joinMethod);
  default:
    return std::make_unique<UnsupportedBootstrapProvider>(joinMethod);
  }
}
} // namespace ToolKit::ToolKitNetworking
