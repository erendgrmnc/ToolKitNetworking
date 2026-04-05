#pragma once

#include <map>
#include <memory>
#include <Types.h>

namespace ToolKit::ToolKitNetworking {
using SessionDirectoryBrokerHeaderMap = std::map<String, String>;

enum class SessionDirectoryBrokerTransportError {
  None,
  Timeout,
  Unreachable,
  InvalidResponse
};

struct SessionDirectoryBrokerTransportRequest {
  String method;
  String path;
  String body;
  String contentType = "application/x-toolkit-session-directory-v1";
  String requestId;
  SessionDirectoryBrokerHeaderMap headers;
};

struct SessionDirectoryBrokerTransportResponse {
  bool success = false;
  SessionDirectoryBrokerTransportError transportError =
      SessionDirectoryBrokerTransportError::None;
  int statusCode = 0;
  String body;
  String detailMessage;
  String correlationId;
  SessionDirectoryBrokerHeaderMap headers;
};

class ISessionDirectoryBrokerTransport {
public:
  virtual ~ISessionDirectoryBrokerTransport() = default;

  virtual SessionDirectoryBrokerTransportResponse Send(
      const SessionDirectoryBrokerTransportRequest &request) const = 0;
};

using SessionDirectoryBrokerTransportPtr =
    std::shared_ptr<ISessionDirectoryBrokerTransport>;
} // namespace ToolKit::ToolKitNetworking
