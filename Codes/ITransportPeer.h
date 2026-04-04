#pragma once

#include "NetworkBase.h"
#include "TransportTypes.h"
#include <string>

namespace ToolKit::ToolKitNetworking {
class ITransportPeer {
public:
  virtual ~ITransportPeer() = default;

  virtual bool Connect(const std::string &host, int portNum) = 0;
  virtual bool UpdateClient() = 0;
  virtual bool GetIsConnected() const = 0;
  virtual TransportPeerId GetPeerID() const = 0;
  virtual void SendPacket(GamePacket &payload, bool reliable = false) = 0;
  virtual void Disconnect() = 0;
  virtual std::string GetIPAddress() = 0;

  virtual void RegisterPacketHandler(int msgID, PacketReceiver *receiver) = 0;
  virtual void ClearPacketHandlers() = 0;
};
} // namespace ToolKit::ToolKitNetworking
