#pragma once

#include "net_common.h"

#include <unordered_map>
#include <thread>
#include <atomic>

class AsyncHost
{
public:
    void Run();
    void Abort();

    bool IsRunning() const { return m_runWorker; }

    // the info we are tracking about each player in the game
    struct PeerInfo
    {
        // is this player slot active
        bool Active;

        // have they sent us a valid position yet?
        bool ValidPosition;

        // the network connection they use
        ENetPeer* Peer;

        // the last known location in X and Y
        int16_t X;
        int16_t Y;

        int16_t DX;
        int16_t DY;

        uint8_t PlayerID = 0;
    };


private:
    std::thread m_worker;
    std::atomic_bool m_runWorker = false;
    uint8_t m_lastPlayerId = 0;

    std::unordered_map<uint32_t, PeerInfo> m_peers;

    void NetworkLoop();
    
    void SendToAllBut(ENetPacket* packet, int exceptPlayerId);


};