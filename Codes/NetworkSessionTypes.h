#pragma once

#include <Types.h>

namespace ToolKit::ToolKitNetworking {

enum class HostingMode {
  None,
  Client,
  DedicatedServer,
  ListenServer
};

enum class JoinMethod {
  DirectAddress,
  SessionDirectory,
  LanDiscovery,
  BrokeredHostedSession
};

enum class ConnectionState {
  Idle,
  StartingHost,
  Discovering,
  Resolving,
  Connecting,
  Handshaking,
  Connected,
  Disconnecting,
  Disconnected,
  Failed,
  Reconnecting
};

enum class DisconnectReason {
  None,
  UserRequested,
  Timeout,
  TransportError,
  BootstrapFailed,
  VersionMismatch,
  AuthRejected,
  ServerShutdown,
  SessionClosed,
  ProtocolError
};

enum class EndpointUsage {
  Bind,
  Advertised,
  JoinTarget,
  ResolvedTransport
};

enum class TransportProtocol {
  EnetUdp
};

namespace SessionProtocol {
constexpr uint Version = 1;
constexpr uint BuildCompatibilityRevision = 1;
constexpr uint DefaultConnectionTimeoutMs = 10000;
constexpr uint DefaultHandshakeTimeoutMs = 5000;
} // namespace SessionProtocol

struct NetworkEndpoint {
  String host;
  uint16 port = 0;
  EndpointUsage usage = EndpointUsage::ResolvedTransport;
  TransportProtocol protocol = TransportProtocol::EnetUdp;

  bool IsConfigured() const { return !host.empty() && port != 0; }
};

struct SessionDescriptor {
  String sessionId;
  HostingMode hostingMode = HostingMode::None;
  JoinMethod joinMethod = JoinMethod::DirectAddress;
  NetworkEndpoint bindEndpoint;
  NetworkEndpoint advertisedEndpoint;
  NetworkEndpoint resolvedEndpoint;
  String buildCompatibilityId;
  bool relayRequired = false;
  bool isJoinCredentialRequired = false;
};

struct SessionJoinRequest {
  JoinMethod joinMethod = JoinMethod::DirectAddress;
  String sessionId;
  String joinCredential;
  NetworkEndpoint targetEndpoint;
  String buildCompatibilityId;
  bool allowDirectHostedConnections = true;
  uint connectionTimeoutMs = SessionProtocol::DefaultConnectionTimeoutMs;
  uint handshakeTimeoutMs = SessionProtocol::DefaultHandshakeTimeoutMs;
};

struct SessionHostRequest {
  HostingMode hostingMode = HostingMode::None;
  NetworkEndpoint bindEndpoint;
  NetworkEndpoint advertisedEndpoint;
  String sessionId;
  String joinCredential;
  String buildCompatibilityId;
  uint maxClients = 2;
  bool enableLanDiscovery = false;
  bool enableRelayFallback = false;
  bool allowDirectHostedConnections = true;
  bool requireJoinCredential = false;
};

struct ConnectionStatus {
  ConnectionState state = ConnectionState::Idle;
  DisconnectReason disconnectReason = DisconnectReason::None;
  String detailMessage;
  String sessionId;
  NetworkEndpoint activeEndpoint;
  bool isAuthenticated = false;
};

} // namespace ToolKit::ToolKitNetworking
