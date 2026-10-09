/* Console smoke test: register -> login -> room auth -> create/list/join */
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

static void on_list(const struct pf_room_info *rooms, int n, void *)
{
    printf("  [list] %d room(s)\n", n);
    for(int i = 0; i < n; ++i)
        printf("    id=%u name=%s players=%d/%d state=%d\n",
               rooms[i].id, rooms[i].name, rooms[i].player_count,
               rooms[i].max_players, rooms[i].state);
}

static void on_room(const struct pf_room_info *room, int err, void *)
{
    if(err)
        printf("  [room] err=%d\n", err);
    else
        printf("  [room] id=%u name=%s players=%d owner=%lld\n",
               room->id, room->name, room->player_count, (long long)room->owner_uid);
}

int main()
{
    char user[64];
    snprintf(user, sizeof(user), "smoke_%lld",
             (long long)std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    const char *pass = "pass1234";
    printf("=== smoke test user=%s ===\n", user);

    /* 1) login missing user -> expect NOT_FOUND(2) */
    if(!LoginClient_Connect("127.0.0.1", 27000)) {
        printf("FAIL connect login\n");
        return 1;
    }
    LoginClient_Login(user, pass);
    bool ok = false;
    int err = -1;
    if(!pump_until(LoginClient_TakeLoginResult, &ok, &err, 3000)) {
        printf("FAIL login timeout\n");
        return 1;
    }
    printf("login(missing) ok=%d err=%d (expect err=2 NOT_FOUND)\n", ok, err);
    if(ok || err != 2) {
        printf("FAIL expected NOT_FOUND\n");
        return 1;
    }

    /* 2) register */
    LoginClient_Register(user, pass);
    if(!pump_until(LoginClient_TakeRegResult, &ok, &err, 3000)) {
        printf("FAIL reg timeout\n");
        return 1;
    }
    printf("register ok=%d err=%d\n", ok, err);
    if(!ok) {
        printf("FAIL register\n");
        return 1;
    }

    /* 3) login ok */
    LoginClient_Login(user, pass);
    if(!pump_until(LoginClient_TakeLoginResult, &ok, &err, 3000)) {
        printf("FAIL login2 timeout\n");
        return 1;
    }
    printf("login ok=%d err=%d uid=%lld token_len=%zu\n", ok, err,
           (long long)LoginClient_Uid(), strlen(LoginClient_Token()));
    if(!ok) {
        printf("FAIL login\n");
        return 1;
    }

    /* 4) bad password -> BAD_PASS(8) */
    LoginClient_Login(user, "wrongpass");
    if(!pump_until(LoginClient_TakeLoginResult, &ok, &err, 3000)) {
        printf("FAIL badpass timeout\n");
        return 1;
    }
    printf("login(badpass) ok=%d err=%d (expect err=8 BAD_PASS)\n", ok, err);
    if(ok || err != 8) {
        printf("FAIL expected BAD_PASS\n");
        return 1;
    }

    /* re-login good */
    LoginClient_Login(user, pass);
    if(!pump_until(LoginClient_TakeLoginResult, &ok, &err, 3000) || !ok) {
        printf("FAIL re-login\n");
        return 1;
    }

    /* 5) room auth + create + list */
    RoomClient_SetCallbacks(on_list, on_room, nullptr, nullptr);
    if(!RoomClient_Connect("127.0.0.1", 27001)) {
        printf("FAIL connect room\n");
        return 1;
    }
    RoomClient_Auth(LoginClient_Token(), LoginClient_Uid(), LoginClient_Username());
    if(!pump_until(RoomClient_TakeAuthResult, &ok, &err, 3000) || !ok) {
        printf("FAIL room auth err=%d\n", err);
        return 1;
    }
    printf("room auth ok\n");

    RoomClient_CreateRoom("smoke_room", "maps/demo.map", 4);
    for(int i = 0; i < 50; ++i) {
        RoomClient_Service();
        if(RoomClient_CurrentRoomId())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    uint32_t rid = RoomClient_CurrentRoomId();
    printf("create room -> current_id=%u\n", rid);
    if(!rid) {
        printf("FAIL create room\n");
        return 1;
    }

    RoomClient_ListRooms();
    for(int i = 0; i < 30; ++i) {
        RoomClient_Service();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    RoomClient_StartMatch(12345, 10, 2);
    bool match = false;
    for(int i = 0; i < 50; ++i) {
        RoomClient_Service();
        if(RoomClient_TakeMatchStarted()) {
            match = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    printf("match_start ok=%d slot=%d kcp_conv=%u udp=%u\n", match,
           RoomClient_LocalSlot(), RoomClient_KcpConv(), RoomClient_UdpPort());
    if(!match) {
        printf("FAIL match_start\n");
        return 1;
    }

    if(!RoomClient_EnterMatch("127.0.0.1")) {
        printf("FAIL EnterMatch\n");
        return 1;
    }
    printf("EnterMatch ok — pumping ticks...\n");
    for(int i = 0; i < 20; ++i) {
        RoomClient_MatchService();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    struct pf_match_peer peers[8];
    int pn = 8;
    RoomClient_MatchGetPeers(peers, &pn);
    printf("in_match tick=%u peers=%d\n", RoomClient_MatchTick(), pn);
    for(int i = 0; i < pn; ++i)
        printf("  slot=%d user=%s in_game=%d tick=%u\n",
               peers[i].slot, peers[i].username, peers[i].in_game, peers[i].last_tick);
    if(RoomClient_MatchTick() == 0) {
        printf("FAIL no ticks advanced\n");
        return 1;
    }
    RoomClient_LeaveMatch();

    printf("=== ALL PASSED ===\n");
    LoginClient_Disconnect();
    RoomClient_Disconnect();
    return 0;
}
