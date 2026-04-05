#pragma once

#include "ITransportHost.h"
#include "ITransportPeer.h"
#include "NetworkPackets.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace ToolKit::ToolKitNetworking {
struct SentPacketRecord {
  TransportPeerId peerId = -1;
  int type = None;
  bool reliable = false;
  std::vector<char> bytes;

  template <typename T> const T *As() const {
    if (bytes.size() != sizeof(T)) {
      return nullptr;
    }

    return reinterpret_cast<const T *>(bytes.data());
  }
};

class FakeTransportHost : public ITransportHost {
public:
  bool IsInitialised() const override { return initialised; }
  void Shutdown() override { shutdownCalls++; }

  bool SendGlobalReliablePacket(GamePacket &packet) const override {
    return SendGlobalPacket(packet, true);
  }

  bool SendGlobalPacket(GamePacket &packet, bool reliable = false) const override {
    return SendPacketToPeer(-1, packet, reliable);
  }

  bool SendGlobalPacket(int messageID) const override {
    GamePacket packet(static_cast<short>(messageID));
    return SendPacketToPeer(-1, packet, false);
  }

  bool SendPacketToPeer(TransportPeerId peerID, GamePacket &packet,
                        bool reliable = false) const override {
    SentPacketRecord record;
    record.peerId = peerID;
    record.type = packet.type;
    record.reliable = reliable;
    record.bytes.resize(static_cast<size_t>(packet.GetTotalSize()));
    std::memcpy(record.bytes.data(), &packet, record.bytes.size());
    sentPackets.push_back(record);
    return true;
  }

  void AddPeer(TransportPeerId peerID) override {
    if (std::find(connectedPeers.begin(), connectedPeers.end(), peerID) ==
        connectedPeers.end()) {
      connectedPeers.push_back(peerID);
    }
  }

  void RemovePeer(TransportPeerId peerID) override {
    connectedPeers.erase(
        std::remove(connectedPeers.begin(), connectedPeers.end(), peerID),
        connectedPeers.end());
  }

  int GetConnectedPeerCount() const override {
    return static_cast<int>(connectedPeers.size());
  }

  const std::vector<TransportPeerId> &GetConnectedPeers() const override {
    return connectedPeers;
  }

  std::string GetIpAddress() const override { return "127.0.0.1"; }
  void UpdateServer() override {}
  int GetServerTick() const override { return 0; }
  void RegisterPacketHandler(int, PacketReceiver *) override {}
  void ClearPacketHandlers() override {}

  const SentPacketRecord *FindLastPacketForPeer(int type,
                                                TransportPeerId peerId) const {
    for (auto it = sentPackets.rbegin(); it != sentPackets.rend(); ++it) {
      if (it->type == type && it->peerId == peerId) {
        return &(*it);
      }
    }

    return nullptr;
  }

public:
  mutable std::vector<SentPacketRecord> sentPackets;
  std::vector<TransportPeerId> connectedPeers;
  bool initialised = true;
  int shutdownCalls = 0;
};

class FakeTransportPeer : public ITransportPeer {
public:
  bool Connect(const std::string &host, int portNum) override {
    connectedHost = host;
    connectedPort = portNum;
    connected = connectResult;
    return connectResult;
  }

  bool UpdateClient() override { return connected; }
  bool GetIsConnected() const override { return connected; }
  TransportPeerId GetPeerID() const override { return peerID; }
  void SetPeerID(TransportPeerId peerId) override { peerID = peerId; }

  void SendPacket(GamePacket &payload, bool reliable = false) override {
    SentPacketRecord record;
    record.type = payload.type;
    record.reliable = reliable;
    record.bytes.resize(static_cast<size_t>(payload.GetTotalSize()));
    std::memcpy(record.bytes.data(), &payload, record.bytes.size());
    sentPackets.push_back(record);
  }

  void Disconnect() override {
    connected = false;
    disconnectCalls++;
  }

  std::string GetIPAddress() override { return connectedHost; }
  void RegisterPacketHandler(int, PacketReceiver *) override {}
  void ClearPacketHandlers() override {}

public:
  std::vector<SentPacketRecord> sentPackets;
  std::string connectedHost = "127.0.0.1";
  int connectedPort = 0;
  bool connectResult = true;
  bool connected = false;
  int disconnectCalls = 0;
  TransportPeerId peerID = -1;
};
} // namespace ToolKit::ToolKitNetworking
