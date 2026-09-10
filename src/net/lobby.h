#ifndef LOBBY_CLIENT_H
#define LOBBY_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#define LOBBY_MAX_ROOMS 32
#define LOBBY_DEFAULT_PORT 27000

struct lobby_room{
    int      id;
    char     name[64];
    char     host[64];
    uint16_t port;
    int      players;
    int      max_players;
    uint32_t seed;
};

int  Lobby_ListRooms(const char *lobby_host, uint16_t lobby_port,
                     struct lobby_room *out, int max_rooms);

int  Lobby_RegisterRoom(const char *lobby_host, uint16_t lobby_port,
                        const char *name, const char *game_host, uint16_t game_port,
                        int max_players, uint32_t seed);

bool Lobby_Heartbeat(const char *lobby_host, uint16_t lobby_port,
                     int room_id, int players);

bool Lobby_Unregister(const char *lobby_host, uint16_t lobby_port, int room_id);

#endif
