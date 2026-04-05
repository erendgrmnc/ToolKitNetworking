#include "HandshakeSecurity.h"
#include <gtest/gtest.h>

namespace ToolKit::ToolKitNetworking {
TEST(HandshakeSecurityTest, FixedHandshakePacketsRequireExactSize) {
  HandshakeHelloPacket hello;
  EXPECT_TRUE(
      HandshakeSecurity::HasExpectedFixedPacketSize(NetworkMessage::HandshakeHello,
                                                    &hello));

  hello.size -= 1;
  EXPECT_FALSE(
      HandshakeSecurity::HasExpectedFixedPacketSize(NetworkMessage::HandshakeHello,
                                                    &hello));
}

TEST(HandshakeSecurityTest, PreAuthAllowlistRejectsReplicationTraffic) {
  EXPECT_TRUE(
      HandshakeSecurity::IsAllowedPreAuthMessage(NetworkMessage::HandshakeHello));
  EXPECT_TRUE(
      HandshakeSecurity::IsAllowedPreAuthMessage(NetworkMessage::HandshakeReject));
  EXPECT_TRUE(
      HandshakeSecurity::IsAllowedPreAuthMessage(NetworkMessage::Shutdown));
  EXPECT_FALSE(
      HandshakeSecurity::IsAllowedPreAuthMessage(NetworkMessage::Snapshot));
  EXPECT_FALSE(HandshakeSecurity::IsAllowedPreAuthMessage(NetworkMessage::RPC));
}

TEST(HandshakeSecurityTest, InvalidAttemptsTriggerTemporaryBlock) {
  HandshakeSecurity::PeerHandshakeGateState state;

  HandshakeSecurity::RecordInvalidAttempt(state, 1000);
  EXPECT_FALSE(HandshakeSecurity::IsPeerBlocked(state, 1000));
  HandshakeSecurity::RecordInvalidAttempt(state, 2000);
  EXPECT_FALSE(HandshakeSecurity::IsPeerBlocked(state, 2000));
  HandshakeSecurity::RecordInvalidAttempt(state, 3000);
  EXPECT_TRUE(HandshakeSecurity::IsPeerBlocked(state, 3000));
  EXPECT_FALSE(HandshakeSecurity::IsPeerBlocked(state, 9000));
}

TEST(HandshakeSecurityTest, DuplicateHelloAndResponseAreDetected) {
  HandshakeSecurity::PeerHandshakeGateState state;
  EXPECT_FALSE(HandshakeSecurity::IsDuplicateOrStaleHello(state));
  EXPECT_TRUE(HandshakeSecurity::IsDuplicateOrStaleResponse(state));

  state.challengeSent = true;
  EXPECT_TRUE(HandshakeSecurity::IsDuplicateOrStaleHello(state));
  EXPECT_FALSE(HandshakeSecurity::IsDuplicateOrStaleResponse(state));

  state.challengeConsumed = true;
  EXPECT_TRUE(HandshakeSecurity::IsDuplicateOrStaleResponse(state));

  state = HandshakeSecurity::PeerHandshakeGateState{};
  state.authenticated = true;
  EXPECT_TRUE(HandshakeSecurity::IsDuplicateOrStaleHello(state));
}
} // namespace ToolKit::ToolKitNetworking
