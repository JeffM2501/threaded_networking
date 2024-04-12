#pragma once

#include "net_common.h"

class AsyncConnection
{
public:
    void SetPeer(ENetPeer* peer) { m_peer = peer; }

    // TODO, add inbound and outbound messages, and status updates

private:
    ENetPeer* m_peer = nullptr;
    uint8_t m_connectionId = 0;
};
