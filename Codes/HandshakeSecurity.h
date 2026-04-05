#pragma once

#include "NetworkPackets.h"
#include "NetworkSessionTypes.h"

namespace ToolKit::ToolKitNetworking {
namespace HandshakeSecurity {
constexpr uint32_t InvalidAttemptThreshold = 3;
constexpr uint64_t InvalidAttemptWindowMs = 5000;
constexpr uint64_t BlockDurationMs = 5000;

struct PeerHandshakeGateState {
  bool challengeSent = false;
  bool challengeConsumed = false;
  bool authenticated = false;
  uint64_t clientNonce = 0;
  uint64_t serverNonce = 0;
  uint32_t invalidAttempts = 0;
  uint64_t invalidWindowStartedAtMs = 0;
  uint64_t blockedUntilMs = 0;
};

bool HasExpectedFixedPacketSize(int type, const GamePacket *packet);
bool IsAllowedPreAuthMessage(int type);
bool IsPeerBlocked(const PeerHandshakeGateState &state, uint64_t nowMs);
void RecordInvalidAttempt(PeerHandshakeGateState &state, uint64_t nowMs);
void ResetChallenge(PeerHandshakeGateState &state);
bool HasPendingChallenge(const PeerHandshakeGateState &state);
bool IsDuplicateOrStaleHello(const PeerHandshakeGateState &state);
bool IsDuplicateOrStaleResponse(const PeerHandshakeGateState &state);
} // namespace HandshakeSecurity
} // namespace ToolKit::ToolKitNetworking
