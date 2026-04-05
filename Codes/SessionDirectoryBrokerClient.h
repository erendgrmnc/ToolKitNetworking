#pragma once

#include "NetworkSessionTypes.h"
#include <memory>

namespace ToolKit::ToolKitNetworking {
enum class SessionDirectoryBrokerError {
  None,
  InvalidRequest,
  SessionNotFound,
  SessionAlreadyExists,
  VersionMismatch,
  Unauthorized,
  RateLimited,
  Timeout,
  ServiceUnavailable,
  ProtocolError
};

struct SessionDirectoryBrokerRegisterRequest {
  HostingMode hostingMode = HostingMode::None;
  String requestedSessionId;
  String requestedJoinCredential;
  NetworkEndpoint bindEndpoint;
  NetworkEndpoint advertisedEndpoint;
  String buildCompatibilityId;
  bool requireJoinCredential = true;
};

struct SessionDirectoryBrokerRegisterResponse {
  bool success = false;
  SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::None;
  String detailMessage;
  String registrationHandle;
  uint64_t registrationExpiresAtMs = 0;
  SessionDescriptor session;
  NetworkEndpoint resolvedJoinRoute;
  String joinCredential;
  String providerName;
  uint64_t joinCredentialExpiresAtMs = 0;
};

struct SessionDirectoryBrokerLookupRequest {
  String sessionId;
  String buildCompatibilityId;
};

struct SessionDirectoryBrokerLookupResponse {
  bool success = false;
  SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::None;
  String detailMessage;
  SessionDescriptor session;
  NetworkEndpoint resolvedJoinRoute;
  String joinCredential;
  String providerName;
  uint64_t joinCredentialExpiresAtMs = 0;
};

struct SessionDirectoryBrokerRefreshRequest {
  String registrationHandle;
};

struct SessionDirectoryBrokerRefreshResponse {
  bool success = false;
  SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::None;
  String detailMessage;
  uint64_t registrationExpiresAtMs = 0;
};

struct SessionDirectoryBrokerUnregisterRequest {
  String registrationHandle;
};

struct SessionDirectoryBrokerUnregisterResponse {
  bool success = false;
  SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::None;
  String detailMessage;
};

class ISessionDirectoryBrokerClient {
public:
  virtual ~ISessionDirectoryBrokerClient() = default;

  virtual SessionDirectoryBrokerRegisterResponse RegisterSession(
      const SessionDirectoryBrokerRegisterRequest &request) = 0;
  virtual SessionDirectoryBrokerLookupResponse LookupSession(
      const SessionDirectoryBrokerLookupRequest &request) const = 0;
  virtual SessionDirectoryBrokerRefreshResponse RefreshSessionRegistration(
      const SessionDirectoryBrokerRefreshRequest &request) = 0;
  virtual SessionDirectoryBrokerUnregisterResponse UnregisterSession(
      const SessionDirectoryBrokerUnregisterRequest &request) = 0;
};

using SessionDirectoryBrokerClientPtr =
    std::shared_ptr<ISessionDirectoryBrokerClient>;
} // namespace ToolKit::ToolKitNetworking
