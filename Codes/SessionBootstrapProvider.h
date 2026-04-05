#pragma once

#include "NetworkSessionTypes.h"
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

class ISessionBootstrapProvider {
public:
  virtual ~ISessionBootstrapProvider() = default;

  virtual JoinMethod GetJoinMethod() const = 0;
  virtual BootstrapHostResult PrepareHostSession(
      const SessionHostRequest &request) const = 0;
  virtual BootstrapJoinResult ResolveJoinSession(
      const SessionJoinRequest &request, HostingMode hostingMode) const = 0;
};

using SessionBootstrapProviderPtr = std::unique_ptr<ISessionBootstrapProvider>;

SessionBootstrapProviderPtr CreateBootstrapProvider(JoinMethod joinMethod);
} // namespace ToolKit::ToolKitNetworking
