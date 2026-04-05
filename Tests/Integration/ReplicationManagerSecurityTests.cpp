#include "NetworkManager.h"
#include "Support/FakeTransport.h"
#include <ToolKit.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>

namespace ToolKit::ToolKitNetworking {
namespace {
template <size_t N>
void CopyPacketText(char (&target)[N], const char *value) {
  std::memset(target, 0, N);
  if (value != nullptr) {
    std::memcpy(target, value, (std::min)(N - 1, std::strlen(value)));
  }
}

class ToolKitTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    ToolKit::Main::SetProxy(&m_main);
    m_main.PreInit();
  }

  void TearDown() override {}

private:
  ToolKit::Main m_main;
};

const auto g_toolkitEnvironment =
    ::testing::AddGlobalTestEnvironment(new ToolKitTestEnvironment());

class TestNetworkManager : public NetworkManager {
public:
  TestNetworkManager() { NativeConstruct(true); }

  void ConfigureAsDedicatedServer(uint16_t listenPort = 7777,
                                  uint maxClients = 2,
                                  const String &sessionId = {},
                                  const String &joinCredential = {},
                                  bool requireJoinCredential = false,
                                  const String &buildCompatibilityId = {}) {
    m_role.SetEnum(NetworkRole::DedicatedServer);
    m_listenPort = listenPort;
    m_maxClients = maxClients;
    m_sessionId = sessionId;
    m_joinCredential = joinCredential;
    m_requireJoinCredential = requireJoinCredential;
    m_buildCompatibilityId = buildCompatibilityId;
  }

  void ConfigureAsClient(const String &host = "127.0.0.1", uint port = 7777,
                         const String &sessionId = {},
                         const String &joinCredential = {},
                         const String &buildCompatibilityId = {}) {
    m_role.SetEnum(NetworkRole::Client);
    m_connectHost = host;
    m_connectPort = port;
    m_sessionId = sessionId;
    m_joinCredential = joinCredential;
    m_buildCompatibilityId = buildCompatibilityId;
  }

  void SetClockNow(uint64_t *nowMs) {
    SetReplicationClockNowProviderForTests([nowMs]() { return *nowMs; });
  }

  std::shared_ptr<FakeTransportHost> GetFakeServer() const { return m_fakeServer; }
  std::shared_ptr<FakeTransportPeer> GetFakeClient() const { return m_fakeClient; }

  bool StartServerTransport(uint16_t port) override {
    lastStartedServerPort = port;
    m_fakeServer = std::make_shared<FakeTransportHost>();
    m_server = m_fakeServer;
    return true;
  }

  bool StartClientTransport(const String &host, uint16_t port) override {
    m_fakeClient = std::make_shared<FakeTransportPeer>();
    m_fakeClient->connectedHost = host.c_str();
    m_fakeClient->connectedPort = static_cast<int>(port);
    m_fakeClient->connectResult = true;
    m_fakeClient->connected = true;
    m_client = m_fakeClient;
    return true;
  }

public:
  uint16_t lastStartedServerPort = 0;

private:
  std::shared_ptr<FakeTransportHost> m_fakeServer;
  std::shared_ptr<FakeTransportPeer> m_fakeClient;
};

HandshakeHelloPacket MakeValidHello(uint64_t clientNonce = 1001) {
  HandshakeHelloPacket hello;
  hello.protocolVersion = SessionProtocol::Version;
  hello.requestedHostingMode = static_cast<uint>(HostingMode::Client);
  hello.clientNonce = clientNonce;
  return hello;
}
} // namespace

TEST(ReplicationManagerSecurityTest, MalformedHandshakeHelloIsRejectedWithProtocolError) {
  TestNetworkManager manager;
  manager.ConfigureAsDedicatedServer();

  ASSERT_TRUE(manager.StartConfiguredSession());
  ASSERT_NE(manager.GetFakeServer(), nullptr);

  HandshakeHelloPacket hello = MakeValidHello();
  hello.size -= 1;
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &hello, 7);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 7);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::ProtocolError));
}

TEST(ReplicationManagerSecurityTest, DuplicateHandshakeHelloIsRejectedAfterChallenge) {
  TestNetworkManager manager;
  manager.ConfigureAsDedicatedServer(7777, 2, "session-alpha", {}, false, "build-42");

  ASSERT_TRUE(manager.StartConfiguredSession());
  HandshakeHelloPacket hello = MakeValidHello(2222);
  CopyPacketText(hello.sessionId, "session-alpha");
  CopyPacketText(hello.buildCompatibilityId, "build-42");

  manager.ReceivePacket(NetworkMessage::HandshakeHello, &hello, 4);
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &hello, 4);

  const SentPacketRecord *challenge =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeChallenge, 4);
  ASSERT_NE(challenge, nullptr);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 4);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::ProtocolError));
  EXPECT_NE(std::string(reject->As<HandshakeRejectPacket>()->detail)
                .find("Duplicate or stale handshake hello"),
            std::string::npos);
}

TEST(ReplicationManagerSecurityTest, ReplayedHandshakeResponseIsRejectedAsStale) {
  TestNetworkManager manager;
  manager.ConfigureAsDedicatedServer(7777, 2, "session-gamma", {}, false, "build-7");

  ASSERT_TRUE(manager.StartConfiguredSession());
  HandshakeHelloPacket hello = MakeValidHello(3333);
  CopyPacketText(hello.sessionId, "session-gamma");
  CopyPacketText(hello.buildCompatibilityId, "build-7");
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &hello, 9);

  const SentPacketRecord *challengeRecord =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeChallenge, 9);
  ASSERT_NE(challengeRecord, nullptr);
  ASSERT_NE(challengeRecord->As<HandshakeChallengePacket>(), nullptr);

  HandshakeResponsePacket response;
  response.clientNonce = challengeRecord->As<HandshakeChallengePacket>()->clientNonce;
  response.serverNonce = challengeRecord->As<HandshakeChallengePacket>()->serverNonce;

  manager.ReceivePacket(NetworkMessage::HandshakeResponse, &response, 9);
  manager.ReceivePacket(NetworkMessage::HandshakeResponse, &response, 9);

  const SentPacketRecord *accept =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeAccept, 9);
  ASSERT_NE(accept, nullptr);
  EXPECT_EQ(manager.GetFakeServer()->GetConnectedPeerCount(), 1);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 9);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::ProtocolError));
}

TEST(ReplicationManagerSecurityTest, PreAuthReplicationTrafficIsRejected) {
  TestNetworkManager manager;
  manager.ConfigureAsDedicatedServer();

  ASSERT_TRUE(manager.StartConfiguredSession());
  WorldSnapshotPacket snapshot;
  manager.ReceivePacket(NetworkMessage::Snapshot, &snapshot, 3);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 3);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::ProtocolError));
}

TEST(ReplicationManagerSecurityTest, PendingHandshakeLimitUsesRateLimitedReason) {
  TestNetworkManager manager;
  manager.ConfigureAsDedicatedServer(7777, 1, "session-delta", {}, false, "build-9");

  ASSERT_TRUE(manager.StartConfiguredSession());
  HandshakeHelloPacket firstHello = MakeValidHello(4444);
  HandshakeHelloPacket secondHello = MakeValidHello(5555);
  CopyPacketText(firstHello.sessionId, "session-delta");
  CopyPacketText(firstHello.buildCompatibilityId, "build-9");
  CopyPacketText(secondHello.sessionId, "session-delta");
  CopyPacketText(secondHello.buildCompatibilityId, "build-9");

  manager.ReceivePacket(NetworkMessage::HandshakeHello, &firstHello, 1);
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &secondHello, 2);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 2);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::RateLimited));
  EXPECT_NE(std::string(reject->As<HandshakeRejectPacket>()->detail)
                .find("pending handshake limit"),
            std::string::npos);
}

TEST(ReplicationManagerSecurityTest, RepeatedInvalidPreAuthTrafficTriggersRateLimitedBlock) {
  TestNetworkManager manager;
  uint64_t nowMs = 1000;
  manager.ConfigureAsDedicatedServer();
  manager.SetClockNow(&nowMs);

  ASSERT_TRUE(manager.StartConfiguredSession());
  HandshakeHelloPacket invalidHello = MakeValidHello();
  invalidHello.protocolVersion = SessionProtocol::Version + 1;

  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 11);
  nowMs += 1000;
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 11);
  nowMs += 1000;
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 11);

  const SentPacketRecord *reject =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 11);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::RateLimited));

  HandshakeHelloPacket validHello = MakeValidHello(9999);
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &validHello, 11);
  reject = manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeReject, 11);
  ASSERT_NE(reject, nullptr);
  ASSERT_NE(reject->As<HandshakeRejectPacket>(), nullptr);
  EXPECT_EQ(reject->As<HandshakeRejectPacket>()->reason,
            static_cast<int>(DisconnectReason::RateLimited));
}

TEST(ReplicationManagerSecurityTest, RateLimitedBlockExpiresAndAllowsRetry) {
  TestNetworkManager manager;
  uint64_t nowMs = 1000;
  manager.ConfigureAsDedicatedServer(7777, 2, "session-epsilon", {}, false, "build-11");
  manager.SetClockNow(&nowMs);

  ASSERT_TRUE(manager.StartConfiguredSession());
  HandshakeHelloPacket invalidHello = MakeValidHello();
  invalidHello.protocolVersion = SessionProtocol::Version + 1;

  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 15);
  nowMs += 1000;
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 15);
  nowMs += 1000;
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &invalidHello, 15);

  nowMs = 9001;
  HandshakeHelloPacket validHello = MakeValidHello(12345);
  CopyPacketText(validHello.sessionId, "session-epsilon");
  CopyPacketText(validHello.buildCompatibilityId, "build-11");
  manager.ReceivePacket(NetworkMessage::HandshakeHello, &validHello, 15);

  const SentPacketRecord *challenge =
      manager.GetFakeServer()->FindLastPacketForPeer(NetworkMessage::HandshakeChallenge, 15);
  ASSERT_NE(challenge, nullptr);
  EXPECT_EQ(challenge->type, NetworkMessage::HandshakeChallenge);
}

TEST(ReplicationManagerSecurityTest, ClientRejectsPreAuthReplicationTrafficBeforeAuthentication) {
  TestNetworkManager manager;
  manager.ConfigureAsClient("server.example.net", 7777, "session-beta", {}, "build-77");

  ASSERT_TRUE(manager.StartConfiguredSession());
  manager.Update(0.0f);

  WorldSnapshotPacket snapshot;
  manager.ReceivePacket(NetworkMessage::Snapshot, &snapshot, -1);

  EXPECT_TRUE(manager.HasSessionAuthFailed());
  EXPECT_EQ(manager.GetSessionAuthFailureReason(),
            DisconnectReason::ProtocolError);
  EXPECT_NE(manager.GetSessionAuthFailureDetail().find("before authentication"),
            String::npos);
}
} // namespace ToolKit::ToolKitNetworking
