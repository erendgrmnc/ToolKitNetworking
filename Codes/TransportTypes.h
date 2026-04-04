#pragma once

namespace ToolKit::ToolKitNetworking {
struct GamePacket;

using TransportPeerId = int;

struct TransportPacketEnvelope {
  int messageType = 0;
  GamePacket *packet = nullptr;
  TransportPeerId peerId = -1;
  bool reliable = false;
};
} // namespace ToolKit::ToolKitNetworking
