#pragma once

#include "INetworkSessionRuntime.h"

namespace ToolKit::ToolKitNetworking {
class FakeSessionRuntime : public INetworkSessionRuntime {
public:
  HostingMode configuredHostingMode = HostingMode::None;
  bool startServerResult = true;
  bool startClientResult = true;
  bool hasServerTransport = false;
  bool hasClientTransport = false;
  bool clientConnected = false;
  uint16_t lastServerPort = 0;
  String lastClientHost;
  uint16_t lastClientPort = 0;
  int startServerCalls = 0;
  int startClientCalls = 0;
  int stopCalls = 0;

  HostingMode GetConfiguredHostingMode() const override {
    return configuredHostingMode;
  }

  bool StartServerTransport(uint16_t port) override {
    ++startServerCalls;
    lastServerPort = port;
    hasServerTransport = startServerResult;
    return startServerResult;
  }

  bool StartClientTransport(const String &host, uint16_t port) override {
    ++startClientCalls;
    lastClientHost = host;
    lastClientPort = port;
    hasClientTransport = startClientResult;
    clientConnected = false;
    return startClientResult;
  }

  void StopSessionTransports() override {
    ++stopCalls;
    hasServerTransport = false;
    hasClientTransport = false;
    clientConnected = false;
  }

  bool HasServerTransport() const override { return hasServerTransport; }
  bool HasClientTransport() const override { return hasClientTransport; }
  bool IsClientTransportConnected() const override { return clientConnected; }
};
} // namespace ToolKit::ToolKitNetworking
