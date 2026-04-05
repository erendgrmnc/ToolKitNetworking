#pragma once

#include "NetworkSessionTypes.h"
#include "SessionDirectoryBrokerClient.h"
#include <functional>
#include <memory>

namespace ToolKit::ToolKitNetworking {
struct SessionDirectoryRegistrationRequest {
  HostingMode hostingMode = HostingMode::None;
  String requestedSessionId;
  String requestedJoinCredential;
  NetworkEndpoint bindEndpoint;
  NetworkEndpoint advertisedEndpoint;
  String buildCompatibilityId;
  bool requireJoinCredential = true;
};

struct SessionDirectoryRegistrationResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  String registrationHandle;
  uint64_t registrationExpiresAtMs = 0;
  SessionDescriptor session;
  NetworkEndpoint resolvedJoinRoute;
  String joinCredential;
  String directoryProviderName;
  uint64_t joinCredentialExpiresAtMs = 0;
};

struct SessionDirectoryLookupRequest {
  String sessionId;
  String buildCompatibilityId;
};

struct SessionDirectoryLookupResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  SessionDescriptor session;
  NetworkEndpoint resolvedJoinRoute;
  String joinCredential;
  String directoryProviderName;
  uint64_t joinCredentialExpiresAtMs = 0;
};

struct SessionDirectoryRefreshRequest {
  String registrationHandle;
};

struct SessionDirectoryRefreshResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  uint64_t registrationExpiresAtMs = 0;
};

struct SessionDirectoryUnregisterRequest {
  String registrationHandle;
};

struct SessionDirectoryUnregisterResult {
  bool success = false;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
};

class ISessionDirectoryService {
public:
  virtual ~ISessionDirectoryService() = default;

  virtual SessionDirectoryRegistrationResult RegisterHostedSession(
      const SessionDirectoryRegistrationRequest &request) = 0;
  virtual SessionDirectoryLookupResult LookupSession(
      const SessionDirectoryLookupRequest &request) const = 0;
  virtual SessionDirectoryRefreshResult RefreshHostedSession(
      const SessionDirectoryRefreshRequest &request) = 0;
  virtual SessionDirectoryUnregisterResult UnregisterHostedSession(
      const SessionDirectoryUnregisterRequest &request) = 0;
};

using SessionDirectoryServicePtr = std::shared_ptr<ISessionDirectoryService>;

SessionDirectoryServicePtr CreateProcessLocalSessionDirectoryService(
    std::function<uint64_t()> nowProvider = {});
SessionDirectoryServicePtr CreateSharedProcessLocalSessionDirectoryService();
SessionDirectoryServicePtr
CreateBrokerBackedSessionDirectoryService(
    SessionDirectoryBrokerClientPtr brokerClient);
SessionDirectoryServicePtr CreateDefaultSessionDirectoryService();
} // namespace ToolKit::ToolKitNetworking
