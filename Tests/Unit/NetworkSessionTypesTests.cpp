#include "NetworkRole.h"
#include "NetworkPackets.h"
#include "SessionDirectoryService.h"
#include "SessionDirectoryRemoteBrokerClient.h"
#include "NetworkSessionCore.h"
#include "NetworkSessionTypes.h"
#include "SessionBootstrapProvider.h"
#include <gtest/gtest.h>

namespace ToolKit::ToolKitNetworking {
namespace {
class FakeSessionDirectoryBrokerClient : public ISessionDirectoryBrokerClient {
public:
  SessionDirectoryBrokerRegisterResponse RegisterSession(
      const SessionDirectoryBrokerRegisterRequest &) override {
    return registerResponse;
  }

  SessionDirectoryBrokerLookupResponse
  LookupSession(const SessionDirectoryBrokerLookupRequest &) const override {
    return lookupResponse;
  }

  SessionDirectoryBrokerRefreshResponse RefreshSessionRegistration(
      const SessionDirectoryBrokerRefreshRequest &) override {
    return refreshResponse;
  }

  SessionDirectoryBrokerUnregisterResponse UnregisterSession(
      const SessionDirectoryBrokerUnregisterRequest &) override {
    return unregisterResponse;
  }

  SessionDirectoryBrokerRegisterResponse registerResponse;
  SessionDirectoryBrokerLookupResponse lookupResponse;
  SessionDirectoryBrokerRefreshResponse refreshResponse;
  SessionDirectoryBrokerUnregisterResponse unregisterResponse;
};

class FakeSessionDirectoryBrokerTransport
    : public ISessionDirectoryBrokerTransport {
public:
  SessionDirectoryBrokerTransportResponse Send(
      const SessionDirectoryBrokerTransportRequest &request) const override {
    lastRequest = request;
    SessionDirectoryBrokerTransportResponse response = nextResponse;
    if (autoPopulateCorrelationId && response.success &&
        response.correlationId.empty()) {
      response.correlationId = request.requestId;
    }
    return response;
  }

  bool autoPopulateCorrelationId = true;
  mutable SessionDirectoryBrokerTransportRequest lastRequest;
  SessionDirectoryBrokerTransportResponse nextResponse;
};
} // namespace

TEST(NetworkSessionTypesTest, NetworkEndpointRequiresHostAndPort) {
  NetworkEndpoint endpoint;
  EXPECT_FALSE(endpoint.IsConfigured());

  endpoint.host = "127.0.0.1";
  EXPECT_FALSE(endpoint.IsConfigured());

  endpoint.port = 7777;
  EXPECT_TRUE(endpoint.IsConfigured());
}

TEST(NetworkSessionTypesTest, SessionJoinRequestDefaultsMatchProtocolConstants) {
  SessionJoinRequest request;

  EXPECT_EQ(request.joinMethod, JoinMethod::DirectAddress);
  EXPECT_EQ(request.connectionTimeoutMs,
            SessionProtocol::DefaultConnectionTimeoutMs);
  EXPECT_EQ(request.handshakeTimeoutMs,
            SessionProtocol::DefaultHandshakeTimeoutMs);
  EXPECT_FALSE(request.targetEndpoint.IsConfigured());
}

TEST(NetworkSessionTypesTest, ConnectionStatusStartsUnauthenticated) {
  ConnectionStatus status;

  EXPECT_EQ(status.state, ConnectionState::Idle);
  EXPECT_EQ(status.disconnectReason, DisconnectReason::None);
  EXPECT_FALSE(status.isAuthenticated);
  EXPECT_FALSE(status.activeEndpoint.IsConfigured());
  EXPECT_FALSE(status.bindEndpoint.IsConfigured());
  EXPECT_FALSE(status.advertisedEndpoint.IsConfigured());
  EXPECT_FALSE(status.resolvedJoinTarget.IsConfigured());
  EXPECT_EQ(status.bootstrapFailureReason, DisconnectReason::None);
  EXPECT_TRUE(status.bootstrapDetail.empty());
  EXPECT_EQ(status.handshakeFailureReason, DisconnectReason::None);
  EXPECT_TRUE(status.handshakeDetail.empty());
}

TEST(NetworkSessionTypesTest, LegacyRoleMapsToExpectedHostingMode) {
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::None),
            HostingMode::None);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::Client),
            HostingMode::Client);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::DedicatedServer),
            HostingMode::DedicatedServer);
  EXPECT_EQ(SessionCore::LegacyRoleToHostingMode(NetworkRole::Host),
            HostingMode::ListenServer);
}

TEST(NetworkSessionTypesTest, CommandLineOverridesParseLegacyHostAndPortFlags) {
  const CommandLineSessionOverrides overrides =
      SessionCore::ParseCommandLineOverrides(
          "\"ToolKitNetworking.exe\" -host -ip 10.0.0.25 -port 9000");

  EXPECT_TRUE(overrides.hasHostingModeOverride);
  EXPECT_EQ(overrides.hostingMode, HostingMode::ListenServer);
  EXPECT_TRUE(overrides.hasConnectHostOverride);
  EXPECT_EQ(overrides.connectHost, "10.0.0.25");
  EXPECT_TRUE(overrides.hasConnectPortOverride);
  EXPECT_EQ(overrides.connectPort, 9000);
  EXPECT_TRUE(overrides.hasListenPortOverride);
  EXPECT_EQ(overrides.listenPort, 9000);
}

TEST(NetworkSessionTypesTest, CommandLineOverridesParseExplicitEndpointFlags) {
  const CommandLineSessionOverrides overrides =
      SessionCore::ParseCommandLineOverrides(
          "\"ToolKitNetworking.exe\" -client -connectHost=wan.example.net "
          "-connectPort=7001 -listenPort=7002 -bindAddress=0.0.0.0 "
          "-advertisedAddress=203.0.113.10");

  EXPECT_TRUE(overrides.hasHostingModeOverride);
  EXPECT_EQ(overrides.hostingMode, HostingMode::Client);
  EXPECT_TRUE(overrides.hasConnectHostOverride);
  EXPECT_EQ(overrides.connectHost, "wan.example.net");
  EXPECT_TRUE(overrides.hasConnectPortOverride);
  EXPECT_EQ(overrides.connectPort, 7001);
  EXPECT_TRUE(overrides.hasListenPortOverride);
  EXPECT_EQ(overrides.listenPort, 7002);
  EXPECT_TRUE(overrides.hasBindAddressOverride);
  EXPECT_EQ(overrides.bindAddress, "0.0.0.0");
  EXPECT_TRUE(overrides.hasAdvertisedAddressOverride);
  EXPECT_EQ(overrides.advertisedAddress, "203.0.113.10");
}

TEST(NetworkSessionTypesTest, CoreBuildsJoinRequestFromConfigAndOverrides) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::Client;
  config.connectHost = "192.168.1.20";
  config.connectPort = 7777;

  CommandLineSessionOverrides overrides;
  overrides.hasConnectHostOverride = true;
  overrides.connectHost = "10.1.1.5";
  overrides.hasConnectPortOverride = true;
  overrides.connectPort = 8888;

  const SessionJoinRequest request =
      SessionCore::BuildJoinRequest(config, overrides);

  EXPECT_EQ(request.targetEndpoint.host, "10.1.1.5");
  EXPECT_EQ(request.targetEndpoint.port, 8888);
  EXPECT_EQ(request.buildCompatibilityId, SessionCore::BuildCompatibilityId());
}

TEST(NetworkSessionTypesTest, HostRequestUsesHostingModeOverride) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::DedicatedServer;
  config.listenPort = 7777;

  CommandLineSessionOverrides overrides;
  overrides.hasHostingModeOverride = true;
  overrides.hostingMode = HostingMode::ListenServer;

  const SessionHostRequest request =
      SessionCore::BuildHostRequest(config, overrides);

  EXPECT_EQ(request.hostingMode, HostingMode::ListenServer);
  EXPECT_EQ(request.bindEndpoint.port, 7777);
}

TEST(NetworkSessionTypesTest, HostRequestUsesBindAndAdvertisedEndpointOverrides) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::DedicatedServer;
  config.listenPort = 7777;
  config.bindAddress = "0.0.0.0";
  config.advertisedAddress = "198.51.100.10";

  CommandLineSessionOverrides overrides;
  overrides.hasListenPortOverride = true;
  overrides.listenPort = 9002;
  overrides.hasBindAddressOverride = true;
  overrides.bindAddress = "192.168.1.25";
  overrides.hasAdvertisedAddressOverride = true;
  overrides.advertisedAddress = "203.0.113.77";

  const SessionHostRequest request =
      SessionCore::BuildHostRequest(config, overrides);

  EXPECT_EQ(request.bindEndpoint.host, "192.168.1.25");
  EXPECT_EQ(request.bindEndpoint.port, 9002);
  EXPECT_EQ(request.advertisedEndpoint.host, "203.0.113.77");
  EXPECT_EQ(request.advertisedEndpoint.port, 9002);
}

TEST(NetworkSessionTypesTest, ListenServerJoinFallsBackToBindAddressWhenConnectHostIsEmpty) {
  SessionBootstrapConfig config;
  config.hostingMode = HostingMode::ListenServer;
  config.connectHost.clear();
  config.connectPort = 0;
  config.listenPort = 7777;
  config.bindAddress = "192.168.1.20";

  const SessionJoinRequest request =
      SessionCore::BuildJoinRequest(config, CommandLineSessionOverrides{});

  EXPECT_EQ(request.targetEndpoint.host, "192.168.1.20");
  EXPECT_EQ(request.targetEndpoint.port, 7777);
}

TEST(NetworkSessionTypesTest, DirectBootstrapProviderResolvesConfiguredJoinTarget) {
  SessionJoinRequest request;
  request.joinMethod = JoinMethod::DirectAddress;
  request.sessionId = "public-session-id";
  request.joinCredential = "secret-token";
  request.targetEndpoint.host = "dedicated.example.net";
  request.targetEndpoint.port = 7777;
  request.buildCompatibilityId = "build-42";

  SessionBootstrapProviderPtr provider =
      CreateBootstrapProvider(JoinMethod::DirectAddress);
  ASSERT_NE(provider, nullptr);

  const BootstrapJoinResult result =
      provider->ResolveJoinSession(request, HostingMode::Client);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.request.targetEndpoint.host, "dedicated.example.net");
  EXPECT_EQ(result.session.sessionId, "public-session-id");
  EXPECT_EQ(result.session.resolvedEndpoint.host, "dedicated.example.net");
  EXPECT_EQ(result.session.resolvedEndpoint.port, 7777);
  EXPECT_TRUE(result.session.isJoinCredentialRequired);
}

TEST(NetworkSessionTypesTest, RedactSecretReturnsPlaceholderForNonEmptySecret) {
  EXPECT_TRUE(SessionCore::RedactSecret("").empty());
  EXPECT_EQ(SessionCore::RedactSecret("secret-token"), "<redacted>");
}

TEST(NetworkSessionTypesTest, SanitizeDiagnosticDetailRedactsConfiguredSecrets) {
  const String sanitized = SessionCore::SanitizeDiagnosticDetail(
      "Bootstrap rejected secret-token during join.",
      {"secret-token"});

  EXPECT_EQ(sanitized.find("secret-token"), String::npos);
  EXPECT_NE(sanitized.find("<redacted>"), String::npos);
}

TEST(NetworkSessionTypesTest, DirectBootstrapProviderRejectsMissingRequiredJoinCredential) {
  SessionHostRequest request;
  request.hostingMode = HostingMode::DedicatedServer;
  request.bindEndpoint.host = "0.0.0.0";
  request.bindEndpoint.port = 7777;
  request.requireJoinCredential = true;

  SessionBootstrapProviderPtr provider =
      CreateBootstrapProvider(JoinMethod::DirectAddress);
  ASSERT_NE(provider, nullptr);

  const BootstrapHostResult result = provider->PrepareHostSession(request);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.disconnectReason, DisconnectReason::BootstrapFailed);
}

TEST(NetworkSessionTypesTest, SessionDirectoryProviderRegistersHostAndResolvesJoin) {
  SessionHostRequest hostRequest;
  hostRequest.hostingMode = HostingMode::DedicatedServer;
  hostRequest.bindEndpoint.host = "192.168.1.20";
  hostRequest.bindEndpoint.port = 7777;
  hostRequest.advertisedEndpoint.host = "directory.example.net";
  hostRequest.advertisedEndpoint.port = 7777;
  hostRequest.buildCompatibilityId = "build-dir-1";

  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  SessionBootstrapProviderPtr provider =
      CreateBootstrapProvider(JoinMethod::SessionDirectory, service);
  ASSERT_NE(provider, nullptr);

  const BootstrapHostResult hostResult = provider->PrepareHostSession(hostRequest);
  ASSERT_TRUE(hostResult.success);
  EXPECT_FALSE(hostResult.request.sessionId.empty());
  EXPECT_FALSE(hostResult.request.joinCredential.empty());
  EXPECT_FALSE(hostResult.request.directoryRegistrationHandle.empty());
  EXPECT_TRUE(hostResult.request.requireJoinCredential);

  SessionJoinRequest joinRequest;
  joinRequest.joinMethod = JoinMethod::SessionDirectory;
  joinRequest.sessionId = hostResult.request.sessionId;
  joinRequest.buildCompatibilityId = "build-dir-1";

  const BootstrapJoinResult joinResult =
      provider->ResolveJoinSession(joinRequest, HostingMode::Client);

  EXPECT_TRUE(joinResult.success);
  EXPECT_EQ(joinResult.request.sessionId, hostResult.request.sessionId);
  EXPECT_EQ(joinResult.request.joinCredential, hostResult.request.joinCredential);
  EXPECT_EQ(joinResult.request.targetEndpoint.host, "directory.example.net");
  EXPECT_EQ(joinResult.request.targetEndpoint.port, 7777);
  EXPECT_EQ(joinResult.session.joinMethod, JoinMethod::SessionDirectory);
  EXPECT_TRUE(joinResult.session.isJoinCredentialRequired);
}

TEST(NetworkSessionTypesTest, SessionDirectoryProviderRejectsWildcardOnlyRoute) {
  SessionHostRequest hostRequest;
  hostRequest.hostingMode = HostingMode::DedicatedServer;
  hostRequest.bindEndpoint.host = "0.0.0.0";
  hostRequest.bindEndpoint.port = 7777;

  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  SessionBootstrapProviderPtr provider =
      CreateBootstrapProvider(JoinMethod::SessionDirectory, service);
  ASSERT_NE(provider, nullptr);

  const BootstrapHostResult hostResult = provider->PrepareHostSession(hostRequest);

  EXPECT_FALSE(hostResult.success);
  EXPECT_EQ(hostResult.disconnectReason, DisconnectReason::BootstrapFailed);
}

TEST(NetworkSessionTypesTest, SessionDirectoryServiceReturnsLookupResultFromRegistration) {
  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.40";
  registration.bindEndpoint.port = 8008;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8008;
  registration.buildCompatibilityId = "build-dir-service";

  const SessionDirectoryRegistrationResult registrationResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = registrationResult.session.sessionId;
  lookup.buildCompatibilityId = "build-dir-service";

  const SessionDirectoryLookupResult lookupResult = service->LookupSession(lookup);

  EXPECT_TRUE(lookupResult.success);
  EXPECT_EQ(lookupResult.session.sessionId, registrationResult.session.sessionId);
  EXPECT_EQ(lookupResult.joinCredential, registrationResult.joinCredential);
  EXPECT_EQ(lookupResult.resolvedJoinRoute.host, "broker.example.net");
  EXPECT_EQ(lookupResult.directoryProviderName, "ProcessLocalSessionDirectory");
}

TEST(NetworkSessionTypesTest, SessionDirectoryServiceRejectsBuildCompatibilityMismatch) {
  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.41";
  registration.bindEndpoint.port = 8009;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8009;
  registration.buildCompatibilityId = "build-dir-compatible";

  const SessionDirectoryRegistrationResult registrationResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = registrationResult.session.sessionId;
  lookup.buildCompatibilityId = "build-dir-incompatible";

  const SessionDirectoryLookupResult lookupResult = service->LookupSession(lookup);

  EXPECT_FALSE(lookupResult.success);
  EXPECT_EQ(lookupResult.disconnectReason, DisconnectReason::VersionMismatch);
  EXPECT_NE(lookupResult.detailMessage.find("not "), String::npos);
}

TEST(NetworkSessionTypesTest, SessionDirectoryServiceRejectsDuplicateRequestedSessionId) {
  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.requestedSessionId = "directory-duplicate-id";
  registration.bindEndpoint.host = "192.168.1.42";
  registration.bindEndpoint.port = 8010;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8010;

  const SessionDirectoryRegistrationResult firstResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(firstResult.success);

  SessionDirectoryRegistrationRequest duplicateRegistration = registration;
  duplicateRegistration.bindEndpoint.host = "192.168.1.43";
  duplicateRegistration.bindEndpoint.port = 8011;
  duplicateRegistration.advertisedEndpoint.port = 8011;

  const SessionDirectoryRegistrationResult duplicateResult =
      service->RegisterHostedSession(duplicateRegistration);

  EXPECT_FALSE(duplicateResult.success);
  EXPECT_EQ(duplicateResult.disconnectReason, DisconnectReason::BootstrapFailed);
  EXPECT_NE(duplicateResult.detailMessage.find("already registered"),
            String::npos);
}

TEST(NetworkSessionTypesTest, SessionDirectoryServiceSupportsRefreshAndUnregisterLifecycle) {
  SessionDirectoryServicePtr service = CreateProcessLocalSessionDirectoryService();
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.44";
  registration.bindEndpoint.port = 8012;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8012;

  const SessionDirectoryRegistrationResult registrationResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);
  ASSERT_FALSE(registrationResult.registrationHandle.empty());
  EXPECT_GT(registrationResult.joinCredentialExpiresAtMs, 0u);
  EXPECT_GT(registrationResult.registrationExpiresAtMs, 0u);

  SessionDirectoryRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = registrationResult.registrationHandle;
  const SessionDirectoryRefreshResult refreshResult =
      service->RefreshHostedSession(refreshRequest);
  EXPECT_TRUE(refreshResult.success);
  EXPECT_GT(refreshResult.registrationExpiresAtMs, 0u);

  SessionDirectoryUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = registrationResult.registrationHandle;
  const SessionDirectoryUnregisterResult unregisterResult =
      service->UnregisterHostedSession(unregisterRequest);
  EXPECT_TRUE(unregisterResult.success);

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = registrationResult.session.sessionId;
  const SessionDirectoryLookupResult lookupResult = service->LookupSession(lookup);
  EXPECT_FALSE(lookupResult.success);
  EXPECT_EQ(lookupResult.disconnectReason, DisconnectReason::SessionClosed);
}

TEST(NetworkSessionTypesTest, SessionDirectoryServiceRejectsExpiredJoinCredentialLookup) {
  uint64_t nowMs = 1000;
  SessionDirectoryServicePtr service =
      CreateProcessLocalSessionDirectoryService([&nowMs]() { return nowMs; });
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.45";
  registration.bindEndpoint.port = 8013;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8013;

  const SessionDirectoryRegistrationResult registrationResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);
  ASSERT_GT(registrationResult.joinCredentialExpiresAtMs, nowMs);
  ASSERT_GT(registrationResult.registrationExpiresAtMs, nowMs);

  nowMs = registrationResult.joinCredentialExpiresAtMs;

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = registrationResult.session.sessionId;

  const SessionDirectoryLookupResult lookupResult = service->LookupSession(lookup);
  EXPECT_FALSE(lookupResult.success);
  EXPECT_EQ(lookupResult.disconnectReason, DisconnectReason::AuthRejected);
  EXPECT_NE(lookupResult.detailMessage.find("expired"), String::npos);

  SessionDirectoryRegistrationRequest reregister = registration;
  reregister.requestedSessionId = registrationResult.session.sessionId;
  const SessionDirectoryRegistrationResult reregisterResult =
      service->RegisterHostedSession(reregister);
  EXPECT_TRUE(reregisterResult.success);
  EXPECT_EQ(reregisterResult.session.sessionId,
            registrationResult.session.sessionId);
}

TEST(NetworkSessionTypesTest, SharedProcessLocalSessionDirectorySupportsExplicitCrossServiceInterop) {
  SessionDirectoryServicePtr sharedDirectory =
      CreateSharedProcessLocalSessionDirectoryService();
  ASSERT_NE(sharedDirectory, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.46";
  registration.bindEndpoint.port = 8014;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8014;
  registration.requestedSessionId = "shared-process-local-session";

  const SessionDirectoryRegistrationResult registrationResult =
      sharedDirectory->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);

  SessionDirectoryServicePtr sameSharedDirectory =
      CreateSharedProcessLocalSessionDirectoryService();
  SessionDirectoryLookupRequest sharedLookup;
  sharedLookup.sessionId = registrationResult.session.sessionId;
  const SessionDirectoryLookupResult lookupResult =
      sameSharedDirectory->LookupSession(sharedLookup);

  ASSERT_TRUE(lookupResult.success);
  EXPECT_EQ(lookupResult.session.sessionId, registrationResult.session.sessionId);

  SessionDirectoryUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = registrationResult.registrationHandle;
  EXPECT_TRUE(sharedDirectory->UnregisterHostedSession(unregisterRequest).success);
}

TEST(NetworkSessionTypesTest, FreshProcessLocalSessionDirectoryInstancesRemainIsolated) {
  SessionDirectoryServicePtr firstDirectory =
      CreateProcessLocalSessionDirectoryService();
  SessionDirectoryServicePtr secondDirectory =
      CreateProcessLocalSessionDirectoryService();
  ASSERT_NE(firstDirectory, nullptr);
  ASSERT_NE(secondDirectory, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.47";
  registration.bindEndpoint.port = 8015;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8015;
  registration.requestedSessionId = "isolated-session";

  const SessionDirectoryRegistrationResult registrationResult =
      firstDirectory->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);

  SessionDirectoryLookupRequest isolatedLookup;
  isolatedLookup.sessionId = registrationResult.session.sessionId;
  SessionDirectoryLookupResult lookupResult =
      secondDirectory->LookupSession(isolatedLookup);
  EXPECT_FALSE(lookupResult.success);
  EXPECT_EQ(lookupResult.disconnectReason, DisconnectReason::SessionClosed);

  SessionDirectoryUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = registrationResult.registrationHandle;
  EXPECT_TRUE(firstDirectory->UnregisterHostedSession(unregisterRequest).success);
}

TEST(NetworkSessionTypesTest, BrokerBackedSessionDirectoryServiceMapsSuccessfulResponses) {
  auto brokerClient = std::make_shared<FakeSessionDirectoryBrokerClient>();
  brokerClient->registerResponse.success = true;
  brokerClient->registerResponse.registrationHandle = "broker-registration";
  brokerClient->registerResponse.registrationExpiresAtMs = 2424;
  brokerClient->registerResponse.providerName = "RemoteBroker";
  brokerClient->registerResponse.joinCredential = "opaque-join-token";
  brokerClient->registerResponse.joinCredentialExpiresAtMs = 4242;
  brokerClient->registerResponse.session.sessionId = "broker-session-id";
  brokerClient->registerResponse.session.hostingMode =
      HostingMode::DedicatedServer;
  brokerClient->registerResponse.session.joinMethod = JoinMethod::SessionDirectory;
  brokerClient->registerResponse.session.buildCompatibilityId = "broker-build";
  brokerClient->registerResponse.resolvedJoinRoute.host = "broker.example.net";
  brokerClient->registerResponse.resolvedJoinRoute.port = 8123;

  brokerClient->lookupResponse.success = true;
  brokerClient->lookupResponse.providerName = "RemoteBroker";
  brokerClient->lookupResponse.joinCredential = "opaque-join-token";
  brokerClient->lookupResponse.joinCredentialExpiresAtMs = 4242;
  brokerClient->lookupResponse.session = brokerClient->registerResponse.session;
  brokerClient->lookupResponse.resolvedJoinRoute =
      brokerClient->registerResponse.resolvedJoinRoute;

  SessionDirectoryServicePtr service =
      CreateBrokerBackedSessionDirectoryService(brokerClient);
  ASSERT_NE(service, nullptr);

  SessionDirectoryRegistrationRequest registration;
  registration.hostingMode = HostingMode::DedicatedServer;
  registration.bindEndpoint.host = "192.168.1.60";
  registration.bindEndpoint.port = 8123;
  registration.advertisedEndpoint.host = "broker.example.net";
  registration.advertisedEndpoint.port = 8123;
  registration.buildCompatibilityId = "broker-build";

  const SessionDirectoryRegistrationResult registrationResult =
      service->RegisterHostedSession(registration);
  ASSERT_TRUE(registrationResult.success);
  EXPECT_EQ(registrationResult.registrationHandle, "broker-registration");
  EXPECT_EQ(registrationResult.directoryProviderName, "RemoteBroker");
  EXPECT_EQ(registrationResult.joinCredential, "opaque-join-token");
  EXPECT_EQ(registrationResult.registrationExpiresAtMs, 2424u);
  EXPECT_EQ(registrationResult.joinCredentialExpiresAtMs, 4242u);
  EXPECT_EQ(registrationResult.session.sessionId, "broker-session-id");
  EXPECT_EQ(registrationResult.resolvedJoinRoute.host, "broker.example.net");

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = "broker-session-id";
  lookup.buildCompatibilityId = "broker-build";

  const SessionDirectoryLookupResult lookupResult = service->LookupSession(lookup);
  ASSERT_TRUE(lookupResult.success);
  EXPECT_EQ(lookupResult.directoryProviderName, "RemoteBroker");
  EXPECT_EQ(lookupResult.joinCredential, "opaque-join-token");
  EXPECT_EQ(lookupResult.joinCredentialExpiresAtMs, 4242u);
  EXPECT_EQ(lookupResult.session.sessionId, "broker-session-id");
  EXPECT_EQ(lookupResult.resolvedJoinRoute.port, 8123);
}

TEST(NetworkSessionTypesTest, BrokerBackedSessionDirectoryServiceMapsBrokerErrorsToDisconnectReasons) {
  auto brokerClient = std::make_shared<FakeSessionDirectoryBrokerClient>();
  SessionDirectoryServicePtr service =
      CreateBrokerBackedSessionDirectoryService(brokerClient);
  ASSERT_NE(service, nullptr);

  SessionDirectoryLookupRequest lookup;
  lookup.sessionId = "broker-session-id";

  brokerClient->lookupResponse = SessionDirectoryBrokerLookupResponse{};
  brokerClient->lookupResponse.errorCode =
      SessionDirectoryBrokerError::SessionNotFound;
  const SessionDirectoryLookupResult notFoundResult = service->LookupSession(lookup);
  EXPECT_FALSE(notFoundResult.success);
  EXPECT_EQ(notFoundResult.disconnectReason, DisconnectReason::SessionClosed);

  brokerClient->lookupResponse = SessionDirectoryBrokerLookupResponse{};
  brokerClient->lookupResponse.errorCode =
      SessionDirectoryBrokerError::RateLimited;
  const SessionDirectoryLookupResult rateLimitedResult =
      service->LookupSession(lookup);
  EXPECT_FALSE(rateLimitedResult.success);
  EXPECT_EQ(rateLimitedResult.disconnectReason, DisconnectReason::RateLimited);

  brokerClient->lookupResponse = SessionDirectoryBrokerLookupResponse{};
  brokerClient->lookupResponse.errorCode = SessionDirectoryBrokerError::Timeout;
  const SessionDirectoryLookupResult timeoutResult = service->LookupSession(lookup);
  EXPECT_FALSE(timeoutResult.success);
  EXPECT_EQ(timeoutResult.disconnectReason, DisconnectReason::Timeout);

  brokerClient->lookupResponse = SessionDirectoryBrokerLookupResponse{};
  brokerClient->lookupResponse.errorCode =
      SessionDirectoryBrokerError::ServiceUnavailable;
  const SessionDirectoryLookupResult unavailableResult =
      service->LookupSession(lookup);
  EXPECT_FALSE(unavailableResult.success);
  EXPECT_EQ(unavailableResult.disconnectReason,
            DisconnectReason::TransportError);

  brokerClient->lookupResponse = SessionDirectoryBrokerLookupResponse{};
  brokerClient->lookupResponse.errorCode =
      SessionDirectoryBrokerError::ProtocolError;
  const SessionDirectoryLookupResult protocolResult =
      service->LookupSession(lookup);
  EXPECT_FALSE(protocolResult.success);
  EXPECT_EQ(protocolResult.disconnectReason, DisconnectReason::ProtocolError);
}

TEST(NetworkSessionTypesTest, BrokerBackedSessionDirectoryServiceSupportsRefreshAndUnregister) {
  auto brokerClient = std::make_shared<FakeSessionDirectoryBrokerClient>();
  brokerClient->refreshResponse.success = true;
  brokerClient->refreshResponse.registrationExpiresAtMs = 9000;
  brokerClient->unregisterResponse.success = true;

  SessionDirectoryServicePtr service =
      CreateBrokerBackedSessionDirectoryService(brokerClient);
  ASSERT_NE(service, nullptr);

  SessionDirectoryRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = "broker-registration";
  const SessionDirectoryRefreshResult refreshResult =
      service->RefreshHostedSession(refreshRequest);
  ASSERT_TRUE(refreshResult.success);
  EXPECT_EQ(refreshResult.registrationExpiresAtMs, 9000u);

  SessionDirectoryUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = "broker-registration";
  const SessionDirectoryUnregisterResult unregisterResult =
      service->UnregisterHostedSession(unregisterRequest);
  EXPECT_TRUE(unregisterResult.success);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientSerializesRegisterRequestAndParsesResponse) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "providerName=RemoteBroker\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "buildCompatibilityId=broker-build\n"
      "bindHost=0.0.0.0\n"
      "bindPort=7777\n"
      "advertisedHost=broker.example.net\n"
      "advertisedPort=7777\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "joinCredential=opaque-join-token\n"
      "joinCredentialExpiresAtMs=4500\n"
      "relayRequired=0\n"
      "requireJoinCredential=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;
  request.requestedSessionId = "preferred-session-id";
  request.requestedJoinCredential = "preferred-join-token";
  request.bindEndpoint.host = "0.0.0.0";
  request.bindEndpoint.port = 7777;
  request.advertisedEndpoint.host = "broker.example.net";
  request.advertisedEndpoint.port = 7777;
  request.buildCompatibilityId = "broker-build";
  request.requireJoinCredential = true;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  ASSERT_TRUE(response.success);
  EXPECT_EQ(transport->lastRequest.method, "POST");
  EXPECT_EQ(transport->lastRequest.path, "/v1/session-directory/register");
  EXPECT_NE(transport->lastRequest.body.find("protocolVersion=1"),
            String::npos);
  EXPECT_NE(transport->lastRequest.body.find("hostingMode=DedicatedServer"),
            String::npos);
  EXPECT_NE(transport->lastRequest.body.find("requestedSessionId=preferred-session-id"),
            String::npos);
  EXPECT_NE(transport->lastRequest.body.find("requestedJoinCredential=preferred-join-token"),
            String::npos);
  EXPECT_NE(transport->lastRequest.body.find("requireJoinCredential=1"),
            String::npos);
  EXPECT_EQ(response.providerName, "RemoteBroker");
  EXPECT_EQ(response.registrationHandle, "broker-registration");
  EXPECT_EQ(response.registrationExpiresAtMs, 4000u);
  EXPECT_EQ(response.session.sessionId, "broker-session-id");
  EXPECT_EQ(response.resolvedJoinRoute.host, "broker.example.net");
  EXPECT_EQ(response.joinCredential, "opaque-join-token");
  EXPECT_EQ(response.joinCredentialExpiresAtMs, 4500u);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientEscapesDelimiterCharactersInRequests) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "providerName=RemoteBroker\n"
      "sessionId=session%3Did%0Aline\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "joinCredential=join%3Dtoken%0Aline\n"
      "joinCredentialExpiresAtMs=4500\n"
      "requireJoinCredential=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;
  request.requestedSessionId = "session=id\nline";
  request.requestedJoinCredential = "join=token\nline";
  request.bindEndpoint.host = "0.0.0.0";
  request.bindEndpoint.port = 7777;
  request.advertisedEndpoint.host = "broker.example.net";
  request.advertisedEndpoint.port = 7777;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  ASSERT_TRUE(response.success);
  EXPECT_NE(transport->lastRequest.body.find("requestedSessionId=session%3Did%0Aline"),
            String::npos);
  EXPECT_NE(transport->lastRequest.body.find("requestedJoinCredential=join%3Dtoken%0Aline"),
            String::npos);
  EXPECT_EQ(response.session.sessionId, "session=id\nline");
  EXPECT_EQ(response.joinCredential, "join=token\nline");
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientSerializesLookupRequestAndParsesErrorBody) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=SessionNotFound\n"
      "detailMessage=Broker%20could%20not%20find%20the%20requested%20session.\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "missing-session";
  request.buildCompatibilityId = "broker-build";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(transport->lastRequest.path, "/v1/session-directory/lookup");
  EXPECT_NE(transport->lastRequest.body.find("sessionId=missing-session"),
            String::npos);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::SessionNotFound);
  EXPECT_EQ(response.detailMessage,
            "Broker could not find the requested session.");
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientMapsTransportAndStatusErrors) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.transportError =
      SessionDirectoryBrokerTransportError::Timeout;
  const SessionDirectoryBrokerLookupResponse timeoutResponse =
      client->LookupSession(request);
  EXPECT_FALSE(timeoutResponse.success);
  EXPECT_EQ(timeoutResponse.errorCode, SessionDirectoryBrokerError::Timeout);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.detailMessage = "Too many requests.";
  const SessionDirectoryBrokerLookupResponse rateLimitedResponse =
      client->LookupSession(request);
  EXPECT_FALSE(rateLimitedResponse.success);
  EXPECT_EQ(rateLimitedResponse.errorCode,
            SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(rateLimitedResponse.detailMessage, "Too many requests.");

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\nsuccess=1\nsessionId=broken\nresolvedHost=\n";
  const SessionDirectoryBrokerLookupResponse malformedResponse =
      client->LookupSession(request);
  EXPECT_FALSE(malformedResponse.success);
  EXPECT_EQ(malformedResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsCorrelationIdMismatch) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->autoPopulateCorrelationId = false;
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.correlationId = "unexpected-request-id";
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "sessionId=broker-session-id\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsCorrelationIdMismatchForRegisterRefreshAndUnregister) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->autoPopulateCorrelationId = false;
  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.correlationId = "unexpected-request-id";
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "sessionId=broker-session-id\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerRegisterRequest registerRequest;
  registerRequest.hostingMode = HostingMode::DedicatedServer;
  const SessionDirectoryBrokerRegisterResponse registerResponse =
      client->RegisterSession(registerRequest);
  EXPECT_FALSE(registerResponse.success);
  EXPECT_EQ(registerResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.correlationId = "unexpected-request-id";
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationExpiresAtMs=9000\n";

  SessionDirectoryBrokerRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerRefreshResponse refreshResponse =
      client->RefreshSessionRegistration(refreshRequest);
  EXPECT_FALSE(refreshResponse.success);
  EXPECT_EQ(refreshResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.correlationId = "unexpected-request-id";
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n";

  SessionDirectoryBrokerUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerUnregisterResponse unregisterResponse =
      client->UnregisterSession(unregisterRequest);
  EXPECT_FALSE(unregisterResponse.success);
  EXPECT_EQ(unregisterResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsMissingCorrelationIdForAllBrokerVerbs) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->autoPopulateCorrelationId = false;
  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "sessionId=broker-session-id\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerRegisterRequest registerRequest;
  registerRequest.hostingMode = HostingMode::DedicatedServer;
  const SessionDirectoryBrokerRegisterResponse registerResponse =
      client->RegisterSession(registerRequest);
  EXPECT_FALSE(registerResponse.success);
  EXPECT_EQ(registerResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "sessionId=broker-session-id\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerLookupRequest lookupRequest;
  lookupRequest.sessionId = "session-alpha";
  const SessionDirectoryBrokerLookupResponse lookupResponse =
      client->LookupSession(lookupRequest);
  EXPECT_FALSE(lookupResponse.success);
  EXPECT_EQ(lookupResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationExpiresAtMs=9000\n";

  SessionDirectoryBrokerRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerRefreshResponse refreshResponse =
      client->RefreshSessionRegistration(refreshRequest);
  EXPECT_FALSE(refreshResponse.success);
  EXPECT_EQ(refreshResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n";

  SessionDirectoryBrokerUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerUnregisterResponse unregisterResponse =
      client->UnregisterSession(unregisterRequest);
  EXPECT_FALSE(unregisterResponse.success);
  EXPECT_EQ(unregisterResponse.errorCode,
            SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsSuccessBodyWithoutExplicitSuccessMarker) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsDuplicateKeysInBrokerPayload) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "sessionId=first\n"
      "sessionId=second\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientParsesStructuredNonSuccessStatusBody) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=RateLimited\n"
      "detailMessage=Back%20off%20and%20retry%20later.\n";
  transport->nextResponse.detailMessage = "Generic rate limit.";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(response.detailMessage, "Back off and retry later.");
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientSanitizesBrokerDetailMessages) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=RateLimited\n"
      "detailMessage=Back%20off%0Aand%09retry.\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(response.detailMessage, "Back off and retry.");
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsInvalidBooleanFields) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "requireJoinCredential=maybe\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsEmptyRequiredSuccessFields) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "sessionId=\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsMalformedEncodedErrorBody) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=RateLimited\n"
      "detailMessage=Broken%2GValue\n";
  transport->nextResponse.detailMessage = "Fallback error.";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(response.detailMessage, "Fallback error.");
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsRegisterSuccessWithoutCredentialExpiryWhenCredentialRequired) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=4000\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "joinCredential=opaque-join-token\n"
      "requireJoinCredential=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsRegisterSuccessWithZeroRegistrationExpiry) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "registrationExpiresAtMs=0\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsLookupSuccessWithoutCredentialExpiryWhenCredentialRequired) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "joinCredential=opaque-join-token\n"
      "requireJoinCredential=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsLookupSuccessWithZeroCredentialExpiryWhenCredentialRequired) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n"
      "joinCredential=opaque-join-token\n"
      "joinCredentialExpiresAtMs=0\n"
      "requireJoinCredential=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerLookupRequest request;
  request.sessionId = "session-alpha";

  const SessionDirectoryBrokerLookupResponse response =
      client->LookupSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientParsesStructuredRefreshAndUnregisterErrors) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=RateLimited\n"
      "detailMessage=Refresh%20later.\n";

  SessionDirectoryBrokerRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerRefreshResponse refreshResponse =
      client->RefreshSessionRegistration(refreshRequest);

  EXPECT_FALSE(refreshResponse.success);
  EXPECT_EQ(refreshResponse.errorCode, SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(refreshResponse.detailMessage, "Refresh later.");

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 404;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=SessionNotFound\n"
      "detailMessage=Already%20gone.\n";

  SessionDirectoryBrokerUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerUnregisterResponse unregisterResponse =
      client->UnregisterSession(unregisterRequest);

  EXPECT_FALSE(unregisterResponse.success);
  EXPECT_EQ(unregisterResponse.errorCode,
            SessionDirectoryBrokerError::SessionNotFound);
  EXPECT_EQ(unregisterResponse.detailMessage, "Already gone.");
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientFallsBackOnMalformedRefreshAndUnregisterErrorBodies) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 429;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=RateLimited\n"
      "detailMessage=Broken%2GValue\n";
  transport->nextResponse.detailMessage = "Refresh fallback.";

  SessionDirectoryBrokerRefreshRequest refreshRequest;
  refreshRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerRefreshResponse refreshResponse =
      client->RefreshSessionRegistration(refreshRequest);

  EXPECT_FALSE(refreshResponse.success);
  EXPECT_EQ(refreshResponse.errorCode, SessionDirectoryBrokerError::RateLimited);
  EXPECT_EQ(refreshResponse.detailMessage, "Refresh fallback.");

  transport->nextResponse = SessionDirectoryBrokerTransportResponse{};
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 404;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=0\n"
      "errorCode=SessionNotFound\n"
      "detailMessage=Broken%2GValue\n";
  transport->nextResponse.detailMessage = "Unregister fallback.";

  SessionDirectoryBrokerUnregisterRequest unregisterRequest;
  unregisterRequest.registrationHandle = "broker-registration";
  const SessionDirectoryBrokerUnregisterResponse unregisterResponse =
      client->UnregisterSession(unregisterRequest);

  EXPECT_FALSE(unregisterResponse.success);
  EXPECT_EQ(unregisterResponse.errorCode,
            SessionDirectoryBrokerError::SessionNotFound);
  EXPECT_EQ(unregisterResponse.detailMessage, "Unregister fallback.");
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsRefreshSuccessWithoutExpiry) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRefreshRequest request;
  request.registrationHandle = "broker-registration";

  const SessionDirectoryBrokerRefreshResponse response =
      client->RefreshSessionRegistration(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsRefreshSuccessWithZeroExpiry) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationExpiresAtMs=0\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRefreshRequest request;
  request.registrationHandle = "broker-registration";

  const SessionDirectoryBrokerRefreshResponse response =
      client->RefreshSessionRegistration(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest,
     RemoteBrokerClientRejectsRegisterSuccessWithoutRegistrationExpiry) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "registrationHandle=broker-registration\n"
      "sessionId=broker-session-id\n"
      "hostingMode=DedicatedServer\n"
      "joinMethod=SessionDirectory\n"
      "resolvedHost=broker.example.net\n"
      "resolvedPort=7777\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerRegisterRequest request;
  request.hostingMode = HostingMode::DedicatedServer;

  const SessionDirectoryBrokerRegisterResponse response =
      client->RegisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, RemoteBrokerClientRejectsMalformedUnregisterSuccessBody) {
  auto transport = std::make_shared<FakeSessionDirectoryBrokerTransport>();
  transport->nextResponse.success = true;
  transport->nextResponse.statusCode = 200;
  transport->nextResponse.body =
      "protocolVersion=1\n"
      "success=1\n"
      "brokenLineWithoutSeparator\n";

  SessionDirectoryBrokerClientPtr client =
      CreateRemoteSessionDirectoryBrokerClient(transport);
  ASSERT_NE(client, nullptr);

  SessionDirectoryBrokerUnregisterRequest request;
  request.registrationHandle = "broker-registration";

  const SessionDirectoryBrokerUnregisterResponse response =
      client->UnregisterSession(request);

  EXPECT_FALSE(response.success);
  EXPECT_EQ(response.errorCode, SessionDirectoryBrokerError::ProtocolError);
}

TEST(NetworkSessionTypesTest, ValidateJoinBootstrapResultNormalizesResolvedEndpoint) {
  SessionJoinRequest request;
  request.targetEndpoint.host = "relay.example.net";
  request.targetEndpoint.port = 9000;

  SessionDescriptor session;
  const SessionValidationResult result =
      SessionCore::ValidateJoinBootstrapResult(request, session);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(session.resolvedEndpoint.host, "relay.example.net");
  EXPECT_EQ(session.resolvedEndpoint.port, 9000);
  EXPECT_EQ(session.resolvedEndpoint.usage, EndpointUsage::ResolvedTransport);
}

TEST(NetworkSessionTypesTest, UnsupportedBootstrapProviderFailsWithoutLeakingCredential) {
  SessionJoinRequest request;
  request.joinMethod = JoinMethod::LanDiscovery;
  request.joinCredential = "do-not-log-me";

  SessionBootstrapProviderPtr provider =
      CreateBootstrapProvider(JoinMethod::LanDiscovery);
  ASSERT_NE(provider, nullptr);

  const BootstrapJoinResult result =
      provider->ResolveJoinSession(request, HostingMode::Client);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.disconnectReason, DisconnectReason::BootstrapFailed);
  EXPECT_EQ(result.detailMessage.find("do-not-log-me"), String::npos);
}

TEST(NetworkSessionTypesTest, PacketStreamRejectsInvalidSkips) {
  PacketStream stream;
  stream.WriteInt(42);

  EXPECT_FALSE(stream.SkipChecked(-1));
  EXPECT_FALSE(stream.SkipChecked(8));
  EXPECT_TRUE(stream.SkipChecked(4));
  EXPECT_EQ(stream.readOffset, 4);
}
} // namespace ToolKit::ToolKitNetworking
