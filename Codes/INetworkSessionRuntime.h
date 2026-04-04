#pragma once

#include "NetworkSessionTypes.h"

namespace ToolKit::ToolKitNetworking {
class INetworkSessionRuntime {
public:
  virtual ~INetworkSessionRuntime() = default;

  virtual HostingMode GetConfiguredHostingMode() const = 0;
  virtual bool StartServerTransport(uint16_t port) = 0;
  virtual bool StartClientTransport(const String &host, uint16_t port) = 0;
  virtual void StopSessionTransports() = 0;
  virtual bool HasServerTransport() const = 0;
  virtual bool HasClientTransport() const = 0;
  virtual bool IsClientTransportConnected() const = 0;
};
} // namespace ToolKit::ToolKitNetworking
