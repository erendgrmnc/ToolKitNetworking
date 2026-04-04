#pragma once

#include "NetworkBase.h"
#include "TransportTypes.h"
#include <string>
#include <vector>

namespace ToolKit::ToolKitNetworking {
class ITransportHost {
public:
  virtual ~ITransportHost() = default;

  virtual bool IsInitialised() const = 0;
  virtual void Shutdown() = 0;
  virtual bool SendGlobalReliablePacket(GamePacket &packet) const = 0;
  virtual bool SendGlobalPacket(GamePacket &packet,
                                bool reliable = false) const = 0;
  virtual bool SendGlobalPacket(int messageID) const = 0;
  virtual bool SendPacketToPeer(TransportPeerId peerID, GamePacket &packet,
                                bool reliable = false) const = 0;
  virtual int GetConnectedPeerCount() const = 0;
  virtual const std::vector<TransportPeerId> &GetConnectedPeers() const = 0;
  virtual std::string GetIpAddress() const = 0;
  virtual void UpdateServer() = 0;
  virtual int GetServerTick() const = 0;

  virtual void RegisterPacketHandler(int msgID, PacketReceiver *receiver) = 0;
  virtual void ClearPacketHandlers() = 0;
};
} // namespace ToolKit::ToolKitNetworking
