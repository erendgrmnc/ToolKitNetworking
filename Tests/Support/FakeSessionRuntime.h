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
  bool sessionAuthenticated = false;
  bool sessionAuthFailed = false;
  bool beginHandshakeResult = true;
  uint16_t lastServerPort = 0;
  String lastClientHost;
  uint16_t lastClientPort = 0;
  SessionJoinRequest lastHandshakeRequest;
  DisconnectReason authFailureReason = DisconnectReason::None;
  String authFailureDetail;
  int startServerCalls = 0;
  int startClientCalls = 0;
  int stopCalls = 0;
  int beginHandshakeCalls = 0;

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
    sessionAuthenticated = false;
    sessionAuthFailed = false;
  }

  bool HasServerTransport() const override { return hasServerTransport; }
  bool HasClientTransport() const override { return hasClientTransport; }
  bool IsClientTransportConnected() const override { return clientConnected; }
  bool BeginSessionHandshake(const SessionJoinRequest &request) override {
    ++beginHandshakeCalls;
    lastHandshakeRequest = request;
    sessionAuthFailed = false;
    if (!beginHandshakeResult) {
      authFailureReason = DisconnectReason::ProtocolError;
      authFailureDetail = "Failed to start handshake.";
    }
    return beginHandshakeResult;
  }
  bool IsSessionAuthenticated() const override { return sessionAuthenticated; }
  bool HasSessionAuthFailed() const override { return sessionAuthFailed; }
  DisconnectReason GetSessionAuthFailureReason() const override {
    return authFailureReason;
  }
  String GetSessionAuthFailureDetail() const override { return authFailureDetail; }
};
} // namespace ToolKit::ToolKitNetworking
