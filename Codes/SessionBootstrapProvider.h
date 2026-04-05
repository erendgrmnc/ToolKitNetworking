#pragma once

#include "NetworkSessionTypes.h"
#include "SessionDirectoryService.h"
#include <memory>

namespace ToolKit::ToolKitNetworking {
struct BootstrapHostResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  SessionHostRequest request;
  SessionDescriptor session;
};

struct BootstrapJoinResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  SessionJoinRequest request;
  SessionDescriptor session;
};

struct HostedSessionRefreshResult {
  bool success = true;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  uint64_t registrationExpiresAtMs = 0;
};

struct HostedSessionReleaseResult {
  bool success = true;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
};

class ISessionBootstrapProvider {
public:
  virtual ~ISessionBootstrapProvider() = default;

  virtual JoinMethod GetJoinMethod() const = 0;
  virtual BootstrapHostResult PrepareHostSession(
      const SessionHostRequest &request) const = 0;
  virtual BootstrapJoinResult ResolveJoinSession(
      const SessionJoinRequest &request, HostingMode hostingMode) const = 0;
  virtual HostedSessionRefreshResult RefreshHostedSession(
      const SessionHostRequest &) {
    return HostedSessionRefreshResult{};
  }
  virtual HostedSessionReleaseResult ReleaseHostedSession(
      const SessionHostRequest &) {
    return HostedSessionReleaseResult{};
  }
};

using SessionBootstrapProviderPtr = std::unique_ptr<ISessionBootstrapProvider>;

SessionBootstrapProviderPtr CreateBootstrapProvider(JoinMethod joinMethod);
SessionBootstrapProviderPtr
CreateBootstrapProvider(JoinMethod joinMethod,
                        SessionDirectoryServicePtr directoryService);
} // namespace ToolKit::ToolKitNetworking
