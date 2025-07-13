
#pragma once
struct _ENetHost;
struct _ENetPeer;
struct _ENetEvent;

#include <enet/enet.h>
#include <map>

namespace ToolKitNetworking {
	struct GamePacket;

	class PacketReceiver {
    public:
        virtual void ReceivePacket(int type, GamePacket* payload, int source = -1) = 0;
    };

    class NetworkBase {
    public:
        static void Initialise();
        static void Destroy();

        static int GetDefaultPort();

        void RegisterPacketHandler(int msgID, PacketReceiver* receiver);

        void ClearPacketHandlers();

    protected:
        NetworkBase();
        ~NetworkBase();

        bool ProcessPacket(GamePacket *p, int peerID = -1) const;

        typedef std::multimap<int, PacketReceiver *>::const_iterator PacketHandlerIterator;

        bool GetPacketHandlers(int msgID, PacketHandlerIterator &first, PacketHandlerIterator &last) const;

        _ENetHost *netHandle;

        std::multimap<int, PacketReceiver *> packetHandlers;
    };
}