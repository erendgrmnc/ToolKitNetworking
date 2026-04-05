#pragma once

#include "SessionDirectoryBrokerClient.h"
#include "SessionDirectoryBrokerTransport.h"

namespace ToolKit::ToolKitNetworking {
class SessionDirectoryRemoteBrokerClient : public ISessionDirectoryBrokerClient {
public:
  explicit SessionDirectoryRemoteBrokerClient(
      SessionDirectoryBrokerTransportPtr transport);

  SessionDirectoryBrokerRegisterResponse RegisterSession(
      const SessionDirectoryBrokerRegisterRequest &request) override;
  SessionDirectoryBrokerLookupResponse LookupSession(
      const SessionDirectoryBrokerLookupRequest &request) const override;
  SessionDirectoryBrokerRefreshResponse RefreshSessionRegistration(
      const SessionDirectoryBrokerRefreshRequest &request) override;
  SessionDirectoryBrokerUnregisterResponse UnregisterSession(
      const SessionDirectoryBrokerUnregisterRequest &request) override;

private:
  SessionDirectoryBrokerTransportPtr m_transport;
};

SessionDirectoryBrokerClientPtr CreateRemoteSessionDirectoryBrokerClient(
    SessionDirectoryBrokerTransportPtr transport);
} // namespace ToolKit::ToolKitNetworking
