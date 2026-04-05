#include "HandshakeSecurity.h"

namespace ToolKit::ToolKitNetworking {
namespace HandshakeSecurity {
namespace {
int ExpectedPacketTotalSize(int type) {
  switch (type) {
  case NetworkMessage::HandshakeHello:
    return static_cast<int>(sizeof(HandshakeHelloPacket));
  case NetworkMessage::HandshakeChallenge:
    return static_cast<int>(sizeof(HandshakeChallengePacket));
  case NetworkMessage::HandshakeResponse:
    return static_cast<int>(sizeof(HandshakeResponsePacket));
  case NetworkMessage::HandshakeAccept:
    return static_cast<int>(sizeof(HandshakeAcceptPacket));
  case NetworkMessage::HandshakeReject:
    return static_cast<int>(sizeof(HandshakeRejectPacket));
  default:
    return 0;
  }
}
} // namespace

bool HasExpectedFixedPacketSize(int type, const GamePacket *packet) {
  if (packet == nullptr) {
    return false;
  }

  const int expectedSize = ExpectedPacketTotalSize(type);
  if (expectedSize == 0) {
    return true;
  }

  return packet->GetTotalSize() == expectedSize;
}

bool IsAllowedPreAuthMessage(int type) {
  switch (type) {
  case NetworkMessage::HandshakeHello:
  case NetworkMessage::HandshakeChallenge:
  case NetworkMessage::HandshakeResponse:
  case NetworkMessage::HandshakeAccept:
  case NetworkMessage::HandshakeReject:
  case NetworkMessage::Shutdown:
  case NetworkMessage::PeerDisconnected:
    return true;
  default:
    return false;
  }
}

bool IsPeerBlocked(const PeerHandshakeGateState &state, uint64_t nowMs) {
  return state.blockedUntilMs != 0 && nowMs < state.blockedUntilMs;
}

void RecordInvalidAttempt(PeerHandshakeGateState &state, uint64_t nowMs) {
  if (state.invalidWindowStartedAtMs == 0 ||
      nowMs - state.invalidWindowStartedAtMs > InvalidAttemptWindowMs) {
    state.invalidWindowStartedAtMs = nowMs;
    state.invalidAttempts = 1;
  } else {
    ++state.invalidAttempts;
  }

  if (state.invalidAttempts >= InvalidAttemptThreshold) {
    state.blockedUntilMs = nowMs + BlockDurationMs;
    ResetChallenge(state);
    state.invalidAttempts = 0;
    state.invalidWindowStartedAtMs = 0;
  }
}

void ResetChallenge(PeerHandshakeGateState &state) {
  state.challengeSent = false;
  state.challengeConsumed = false;
  state.clientNonce = 0;
  state.serverNonce = 0;
}

bool HasPendingChallenge(const PeerHandshakeGateState &state) {
  return state.challengeSent && !state.challengeConsumed && !state.authenticated;
}

bool IsDuplicateOrStaleHello(const PeerHandshakeGateState &state) {
  return state.authenticated || HasPendingChallenge(state) ||
         state.challengeConsumed;
}

bool IsDuplicateOrStaleResponse(const PeerHandshakeGateState &state) {
  return !state.challengeSent || state.challengeConsumed || state.authenticated;
}
} // namespace HandshakeSecurity
} // namespace ToolKit::ToolKitNetworking
