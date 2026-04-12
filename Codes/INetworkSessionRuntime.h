#pragma once

#include "NetworkSessionCore.h"
#include "NetworkSessionTypes.h"
#include "SessionDirectoryService.h"

namespace ToolKit::ToolKitNetworking {
class INetworkSessionRuntime {
public:
  virtual ~INetworkSessionRuntime() = default;

  virtual SessionBootstrapConfig GetSessionBootstrapConfig() const = 0;
  virtual HostingMode GetConfiguredHostingMode() const = 0;
  virtual SessionDirectoryBrokerRuntimeConfig
  GetSessionDirectoryBrokerRuntimeConfig() const = 0;
  virtual SessionDirectoryServiceBuildResult
  BuildSessionDirectoryService(
      const SessionDirectoryBrokerRuntimeConfig &config) const = 0;
  virtual bool StartServerTransport(uint16_t port) = 0;
  virtual bool StartClientTransport(const String &host, uint16_t port) = 0;
  virtual void StopSessionTransports() = 0;
  virtual bool HasServerTransport() const = 0;
  virtual bool HasClientTransport() const = 0;
  virtual bool IsClientTransportConnected() const = 0;
  virtual bool BeginSessionHandshake(const SessionJoinRequest &request) = 0;
  virtual bool IsSessionAuthenticated() const = 0;
  virtual bool HasSessionAuthFailed() const = 0;
  virtual DisconnectReason GetSessionAuthFailureReason() const = 0;
  virtual String GetSessionAuthFailureDetail() const = 0;
};
} // namespace ToolKit::ToolKitNetworking
