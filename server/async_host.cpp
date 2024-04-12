#define ENET_IMPLEMENTATION
#include "net_common.h"

#include "async_host.h"

ENetHost* g_server = nullptr;

void AsyncHost::Run()
{
    // set up networking
    if (enet_initialize() != 0)
        return;

    // network servers must 'listen' on an interface and a port
    // this code sets up enet to listen on any available interface and using our port
    // the client must use the same port as the server and know the address of the server
    ENetAddress address = { 0 };
    address.host = ENET_HOST_ANY;
    address.port = 4545;

    // create the server host
    g_server = enet_host_create(&address, MAX_PLAYERS, 1, 0, 0);

    if (g_server == NULL)
        return;

    printf("Created\n");

    m_runWorker = true;
    m_worker = std::thread([this](){ NetworkLoop(); });
}

void AsyncHost::Abort()
{
    if (m_runWorker)
    {
        m_runWorker = false;
        if (m_worker.joinable())
            m_worker.join();
    }
}

void AsyncHost::NetworkLoop()
{
    while(m_runWorker)
    {
        ENetEvent event;
        event.type = ENET_EVENT_TYPE_NONE;

        // see if there are any inbound network events, wait up to 1000ms before returning.
        // if the server also did game logic, this timeout should be lowered
        if (enet_host_service(g_server, &event, 1000) > 0)
        {
            // see what kind of event we have
            switch (event.type)
            {

                // a new client is trying to connect
            case ENET_EVENT_TYPE_CONNECT:
            {
                printf("Player Connected\n");

                PeerInfo newPeer;
               
                // player is good, don't give away the slot
                newPeer.Active = true;

                // but don't send out an update to everyone until they give us a good position
                newPeer.ValidPosition = false;
                newPeer.Peer = event.peer;

                // pack up a message to send back to the client to tell them they have been accepted as a player
                uint8_t buffer[2] = { 0 };
                buffer[0] = (uint8_t)AcceptPlayer;  // command for the client
                buffer[1] = m_lastPlayerId++;      // the player ID so they know who they are

                // copy the buffer into an enet packet (TODO : add write functions to go directly to a packet)
                ENetPacket* packet = enet_packet_create(buffer, 2, ENET_PACKET_FLAG_RELIABLE);
                // send the data to the user
                enet_peer_send(event.peer, 0, packet);

                // We have to tell the new client about all the other players that are already on the server
                // so send them an add message for all existing active players.
                for (auto &[id,peer] : m_peers)
                {
                    // only people who are valid and not the new player
                    if (!peer.ValidPosition)
                        continue;

                    // pack up an add player message with the ID and the last known position
                    uint8_t addBuffer[10] = { 0 };
                    addBuffer[0] = (uint8_t)AddPlayer;
                    addBuffer[1] = peer.PlayerID;
                    *(int16_t*)(addBuffer + 2) = (int16_t)peer.X;
                    *(int16_t*)(addBuffer + 4) = (int16_t)peer.Y;
                    *(int16_t*)(addBuffer + 6) = (int16_t)peer.DX;
                    *(int16_t*)(addBuffer + 8) = (int16_t)peer.DY;

                    // Optimally we'd also send other info like name, color, and other static player info.

                    // copy and send the message
                    packet = enet_packet_create(addBuffer, 10, ENET_PACKET_FLAG_RELIABLE);
                    enet_peer_send(event.peer, 0, packet);

                    // NOTE enet_host_service will handle releasing send packets when the network system has finally sent them,
                    // you don't have to destroy them
                }

                m_peers.insert_or_assign(event.peer->connectID, newPeer);

                break;
            }

            // someone sent us data
            case ENET_EVENT_TYPE_RECEIVE:
            {
                // keep track of how far into the message we are
                size_t offset = 0;

                PeerInfo& peer = m_peers[event.peer->connectID];

                // read off the command the client wants us to process
                NetworkCommands command = NetworkCommands(ReadByte(event.packet, &offset));

                // we only accept one message from clients for now, so make sure this is what it is
                if (command == UpdateInput)
                {
                    // update the location data with the new info
                    peer.X = ReadShort(event.packet, &offset);
                    peer.Y = ReadShort(event.packet, &offset);
                    peer.DX = ReadShort(event.packet, &offset);
                    peer.DY = ReadShort(event.packet, &offset);

                    // lets tell everyone about this new location
                    NetworkCommands outboundCommand = UpdatePlayer;

                    // if they are new, send this update as an add player instead of an update
                    if (!peer.ValidPosition)
                        outboundCommand = AddPlayer;

                    // the player has sent us a position, they can be part of future regular updates
                    peer.ValidPosition = true;

                    // pack up the update message with command, player and position
                    uint8_t buffer[10] = { 0 };
                    buffer[0] = (uint8_t)outboundCommand;
                    buffer[1] = peer.PlayerID;
                    *(int16_t*)(buffer + 2) = (int16_t)peer.X;
                    *(int16_t*)(buffer + 4) = (int16_t)peer.Y;
                    *(int16_t*)(buffer + 6) = (int16_t)peer.DX;
                    *(int16_t*)(buffer + 8) = (int16_t)peer.DY;


                    // Copy and send the data to everyone but the player who sent it  (TODO : add write functions to go directly to a packet)
                    ENetPacket* packet = enet_packet_create(buffer, 10, ENET_PACKET_FLAG_RELIABLE);
                    SendToAllBut(packet, event.peer->connectID);

                    // NOTE enet_host_service will handle releasing send packets when the network system has finally sent them,
                    // you don't have to destroy them
                }

                // tell enet that it can recycle the inbound packet
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                // a player was disconnected
                printf("Player Disconnected\n");

                auto peerItr = m_peers.find(event.peer->connectID);

                // Tell everyone that someone left
                uint8_t buffer[2] = { 0 };
                buffer[0] = (uint8_t)RemovePlayer;
                buffer[1] = peerItr->second.PlayerID;

                // Copy and send the data to everyone but the player who sent it  (TODO : add write functions to go directly to a packet)
                ENetPacket* packet = enet_packet_create(buffer, 2, ENET_PACKET_FLAG_RELIABLE);
                SendToAllBut(packet, -1);

                m_peers.erase(peerItr);

                // NOTE enet_host_service will handle releasing send packets when the network system has finally sent them,
                // you don't have to destroy them

                break;
            }

            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
    // cleanup
    enet_host_destroy(g_server);
    enet_deinitialize();
    m_runWorker = false;
}

// sends a packet over the network to every active player, except the one specified (usually the sender)
// senders know what they sent so you can choose to not send them data they already know.
// in a truly authoritative server you'd send back an acceptance message to all client input so they know it wasn't rejected.
void AsyncHost::SendToAllBut(ENetPacket* packet, int exceptPlayerId)
{
    for (auto&[id,peer] : m_peers)
    {
        if (!peer.Active || id == exceptPlayerId)
            continue;

        enet_peer_send(peer.Peer, 0, packet);
    }
}
