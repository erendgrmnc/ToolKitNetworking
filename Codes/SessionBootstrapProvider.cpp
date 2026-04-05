#include "SessionBootstrapProvider.h"
#include <map>

namespace ToolKit::ToolKitNetworking {
namespace {
struct SessionDirectoryRecord {
  SessionDescriptor session;
  NetworkEndpoint joinRoute;
  String joinCredential;
};

std::map<String, SessionDirectoryRecord> &SessionDirectoryRegistry() {
  static std::map<String, SessionDirectoryRecord> registry;
  return registry;
}

uint64_t NextGeneratedId() {
  static uint64_t nextId = 1;
  return nextId++;
}

String GenerateOpaqueValue(const char *prefix) {
  return String(prefix) + std::to_string(NextGeneratedId());
}

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
  JoinMethod GetJoinMethod() const override {
    return JoinMethod::SessionDirectory;
  }

  BootstrapHostResult
  PrepareHostSession(const SessionHostRequest &request) const override {
    BootstrapHostResult result;
    result.request = request;

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

    result.success = true;
    result.request.sessionId = request.sessionId.empty()
                                   ? GenerateOpaqueValue("directory-session-")
                                   : request.sessionId;
    result.request.joinCredential = request.joinCredential.empty()
                                        ? GenerateOpaqueValue("directory-key-")
                                        : request.joinCredential;
    result.request.requireJoinCredential = true;

    result.session.sessionId = result.request.sessionId;
    result.session.hostingMode = request.hostingMode;
    result.session.joinMethod = JoinMethod::SessionDirectory;
    result.session.bindEndpoint = request.bindEndpoint;
    result.session.advertisedEndpoint = joinRoute;
    result.session.resolvedEndpoint = joinRoute;
    result.session.resolvedEndpoint.usage = EndpointUsage::ResolvedTransport;
    result.session.buildCompatibilityId = request.buildCompatibilityId;
    result.session.isJoinCredentialRequired = true;

    SessionDirectoryRecord record;
    record.session = result.session;
    record.joinRoute = result.session.resolvedEndpoint;
    record.joinCredential = result.request.joinCredential;
    SessionDirectoryRegistry()[result.request.sessionId] = record;
    return result;
  }

  BootstrapJoinResult ResolveJoinSession(const SessionJoinRequest &request,
                                         HostingMode hostingMode) const override {
    BootstrapJoinResult result;
    result.request = request;

    if (request.sessionId.empty()) {
      result.disconnectReason = DisconnectReason::BootstrapFailed;
      result.detailMessage =
          "Session directory join bootstrap requires a public session "
          "identifier.";
      return result;
    }

    auto it = SessionDirectoryRegistry().find(request.sessionId);
    if (it == SessionDirectoryRegistry().end()) {
      result.disconnectReason = DisconnectReason::SessionClosed;
      result.detailMessage =
          "Requested session identifier could not be resolved from the "
          "session directory.";
      return result;
    }

    result.success = true;
    result.request.sessionId = it->second.session.sessionId;
    result.request.joinCredential = it->second.joinCredential;
    result.request.targetEndpoint = it->second.joinRoute;
    result.session = it->second.session;
    result.session.hostingMode = hostingMode;
    result.session.joinMethod = JoinMethod::SessionDirectory;
    result.session.resolvedEndpoint = it->second.joinRoute;
    result.session.resolvedEndpoint.usage = EndpointUsage::ResolvedTransport;
    result.session.isJoinCredentialRequired = true;
    return result;
  }
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
  switch (joinMethod) {
  case JoinMethod::DirectAddress:
    return std::make_unique<DirectAddressBootstrapProvider>();
  case JoinMethod::SessionDirectory:
    return std::make_unique<SessionDirectoryBootstrapProvider>();
  case JoinMethod::LanDiscovery:
  case JoinMethod::BrokeredHostedSession:
    return std::make_unique<UnsupportedBootstrapProvider>(joinMethod);
  default:
    return std::make_unique<UnsupportedBootstrapProvider>(joinMethod);
  }
}
} // namespace ToolKit::ToolKitNetworking
