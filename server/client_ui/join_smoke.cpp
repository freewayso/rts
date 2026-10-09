/* Two-role smoke: create room as host, or join room_id as guest. */
#include "public/online_client.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static bool pump_until(bool (*take)(bool *, int *), bool *ok, int *err, int ms)
{
    auto t0 = std::chrono::steady_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0)
              .count() < ms) {
        LoginClient_Service();
        RoomClient_Service();
        if(take(ok, err))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void sleep_pump(int ms)
{
    auto t0 = std::chrono::steady_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - t0)
              .count() < ms) {
        LoginClient_Service();
        RoomClient_Service();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static uint32_t g_seen_room;
static int g_seen_err = -999;

static void on_list(const struct pf_room_info *rooms, int n, void *)
{
    printf("  [list] %d room(s)\n", n);
    for(int i = 0; i < n; ++i) {
        printf("    id=%u name=%s %d/%d\n", rooms[i].id, rooms[i].name,
               rooms[i].player_count, rooms[i].max_players);
        if(!g_seen_room)
            g_seen_room = rooms[i].id;
    }
}

static void on_room(const struct pf_room_info *room, int err, void *)
{
    g_seen_err = err;
    if(err > 0) {
        printf("  [room] FAIL err=%d\n", err);
        return;
    }
    if(!room) {
        printf("  [room] null\n");
        return;
    }
    printf("  [room] err=%d id=%u name=%s players=%d\n", err, room->id, room->name,
           room->player_count);
    g_seen_room = room->id;
}

static bool login_as(const char *user, const char *pass)
{
    if(!LoginClient_Connect("127.0.0.1", 27000)) {
        printf("FAIL login connect\n");
        return false;
    }
    LoginClient_Register(user, pass);
    bool ok = false;
    int err = 0;
    pump_until(LoginClient_TakeRegResult, &ok, &err, 2000);
    LoginClient_Login(user, pass);
    if(!pump_until(LoginClient_TakeLoginResult, &ok, &err, 3000) || !ok) {
        printf("FAIL login user=%s err=%d\n", user, err);
        return false;
    }
    printf("login ok user=%s uid=%lld\n", user, (long long)LoginClient_Uid());
    if(!RoomClient_Connect("127.0.0.1", 27001)) {
        printf("FAIL room connect\n");
        return false;
    }
    RoomClient_SetCallbacks(on_list, on_room, nullptr, nullptr);
    RoomClient_Auth(LoginClient_Token(), LoginClient_Uid(), LoginClient_Username());
    if(!pump_until(RoomClient_TakeAuthResult, &ok, &err, 3000) || !ok) {
        printf("FAIL room auth err=%d\n", err);
        return false;
    }
    printf("room auth ok\n");
    return true;
}

int main(int argc, char **argv)
{
    if(argc < 2) {
        printf("usage: join_smoke create|join [room_id]\n");
        return 2;
    }
    const char *mode = argv[1];
    char user[64];
    using clock = std::chrono::system_clock;
    snprintf(user, sizeof(user), "js_%s_%lld", mode,
             (long long)clock::now().time_since_epoch().count() % 1000000);
    const char *pass = "pass1234";

    if(!login_as(user, pass))
        return 1;

    if(strcmp(mode, "create") == 0) {
        RoomClient_CreateRoom("demo", "assets/maps/demo.pfmap", 8);
        sleep_pump(1500);
        uint32_t rid = RoomClient_CurrentRoomId();
        printf("CREATE_OK room_id=%u\n", rid);
        if(!rid)
            return 1;
        /* Stay connected so room is not deleted on disconnect. */
        for(int i = 0; i < 80; ++i) {
            sleep_pump(100);
            printf("host_tick room=%u\n", RoomClient_CurrentRoomId());
        }
        return 0;
    }

    if(strcmp(mode, "join") == 0) {
        uint32_t rid = 0;
        if(argc >= 3)
            rid = (uint32_t)strtoul(argv[2], nullptr, 10);
        RoomClient_ListRooms();
        sleep_pump(800);
        if(!rid)
            rid = g_seen_room;
        printf("joining room_id=%u\n", rid);
        if(!rid) {
            printf("FAIL no room to join\n");
            return 1;
        }
        g_seen_err = -999;
        RoomClient_JoinRoom(rid);
        sleep_pump(1500);
        if(RoomClient_CurrentRoomId() == rid) {
            printf("JOIN_OK room_id=%u\n", rid);
            return 0;
        }
        printf("FAIL join current=%u seen_err=%d\n", RoomClient_CurrentRoomId(), g_seen_err);
        return 1;
    }

    printf("unknown mode %s\n", mode);
    return 2;
}
