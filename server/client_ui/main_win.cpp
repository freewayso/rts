/*
 * Permafrost client launcher (Win32):
 *   Login → choose Solo / Online → Solo starts pf.exe; Online is room lobby.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

#include "net/public/online_client.h"
#include "map_preview.h"
#include "engine_launch.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

enum {
    /* Login */
    ID_HOST = 1001,
    ID_LOGIN_PORT,
    ID_ROOM_PORT,
    ID_USER,
    ID_PASS,
    ID_BTN_REG,
    ID_BTN_LOGIN,
    /* Mode */
    ID_BTN_SOLO,
    ID_BTN_ONLINE,
    ID_BTN_LOGOUT,
    ID_MODE_WELCOME,
    /* Solo */
    ID_SOLO_MAP,
    ID_BTN_SOLO_START,
    ID_BTN_SOLO_BACK,
    /* Online lobby */
    ID_BTN_ENTER_ROOM,
    ID_ROOM_NAME,
    ID_ROOM_MAP,
    ID_BTN_CREATE,
    ID_BTN_REFRESH,
    ID_BTN_JOIN,
    ID_BTN_DELETE,
    ID_BTN_LEAVE,
    ID_BTN_START,
    ID_BTN_LEAVE_MATCH,
    ID_BTN_ONLINE_BACK,
    ID_LIST,
    ID_MATCH_LIST,
    ID_MAP_VIEW,
    ID_STATUS,
    ID_LOG,
    TIMER_PUMP = 1
};

enum UiScreen {
    SCREEN_LOGIN = 0,
    SCREEN_MODE,
    SCREEN_SOLO,
    SCREEN_ONLINE
};

static HWND g_hwnd;
static HWND g_list, g_matchList, g_mapView, g_status, g_log;
static HWND g_host, g_loginPort, g_roomPort, g_user, g_pass;
static HWND g_roomName, g_roomMap, g_soloMap;
static HWND g_btnSoloStart, g_btnStartMatch;
static HWND g_lblLoginTitle, g_lblModeWelcome, g_lblOnlineUser;
static std::vector<HWND> g_ctrls_login, g_ctrls_mode, g_ctrls_solo, g_ctrls_online;
static UiScreen g_screen = SCREEN_LOGIN;
static bool g_logged_in;
static bool g_room_authed;
static bool g_pending_create;
static bool g_in_match_ui;
static bool g_match_starting; /* StartMatch sent, waiting for ntf / engine */
static std::vector<pf_room_info> g_rooms;
static uint32_t g_selected_room;
/* Lobby RoomUpdate can arrive before a late empty list_rsp from an earlier
 * list_rooms; do not let that empty snapshot wipe rooms we just learned about. */
static ULONGLONG g_last_lobby_push_ms;

static void enter_room_server(void);
static void do_create_room(void);
static void refresh_match_peers_ui(void);
static bool begin_match_session(void);
static void paint_map_view(void);
static void show_screen(UiScreen s);
static void update_start_buttons(void);

static void append_log(const wchar_t *line)
{
    if(!g_log) return;
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
}

static void set_status(const wchar_t *s)
{
    if(g_status)
        SetWindowTextW(g_status, s);
}

static void utf8_to_wide(const char *u8, wchar_t *out, int out_n)
{
    MultiByteToWideChar(CP_UTF8, 0, u8 ? u8 : "", -1, out, out_n);
}

static void wide_to_utf8(HWND edit, char *out, int out_n)
{
    wchar_t w[256];
    GetWindowTextW(edit, w, 256);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, out_n, NULL, NULL);
}

static int get_port(HWND edit, int defv)
{
    char b[32];
    wide_to_utf8(edit, b, sizeof(b));
    int v = atoi(b);
    return v > 0 ? v : defv;
}

static const wchar_t *state_name(int st)
{
    switch(st) {
    case 0: return L"OPEN";
    case 1: return L"STARTING";
    case 2: return L"IN_MATCH";
    case 3: return L"CLOSED";
    default: return L"?";
    }
}

static const wchar_t *err_msg(int err)
{
    switch(err) {
    case 0: return L"OK";
    case 1: return L"未授权";
    case 2: return L"不存在（登录=用户未注册；房间=房间已关闭或不存在）";
    case 3: return L"房间已满";
    case 4: return L"不是房主";
    case 5: return L"状态不允许（需≥2人才能开局；已开局则无法再加入）";
    case 6: return L"已在房间内";
    case 7: return L"用户名已存在";
    case 8: return L"密码错误";
    case 9: return L"服务器内部错误";
    case 10: return L"参数错误";
    default: return L"未知错误";
    }
}

static void set_group_visible(const std::vector<HWND> &group, bool vis)
{
    int cmd = vis ? SW_SHOW : SW_HIDE;
    for(HWND h : group) {
        if(h) ShowWindow(h, cmd);
    }
}

static void show_screen(UiScreen s)
{
    g_screen = s;
    set_group_visible(g_ctrls_login, s == SCREEN_LOGIN);
    set_group_visible(g_ctrls_mode, s == SCREEN_MODE);
    set_group_visible(g_ctrls_solo, s == SCREEN_SOLO);
    set_group_visible(g_ctrls_online, s == SCREEN_ONLINE);

    /* Status + log always visible at bottom */
    if(g_status) ShowWindow(g_status, SW_SHOW);
    if(g_log) ShowWindow(g_log, SW_SHOW);

    switch(s) {
    case SCREEN_LOGIN:
        SetWindowTextW(g_hwnd, L"Permafrost — 登录");
        set_status(L"请登录或注册账号");
        break;
    case SCREEN_MODE: {
        SetWindowTextW(g_hwnd, L"Permafrost — 选择模式");
        wchar_t user[64], msg[160];
        utf8_to_wide(LoginClient_Username(), user, 64);
        swprintf(msg, 160, L"你好，%ls — 请选择游戏模式", user);
        if(g_lblModeWelcome)
            SetWindowTextW(g_lblModeWelcome, msg);
        set_status(msg);
        break;
    }
    case SCREEN_SOLO:
        SetWindowTextW(g_hwnd, L"Permafrost — 单机模式");
        set_status(L"单机模式：选择地图后开始游戏（不连房间服）");
        break;
    case SCREEN_ONLINE: {
        wchar_t user[64], title[160], account[192];
        utf8_to_wide(LoginClient_Username(), user, 64);
        if(!user[0])
            wcsncpy(user, L"(未登录)", 63);
        swprintf(title, 160, L"Permafrost — 联机大厅 [%ls]", user);
        SetWindowTextW(g_hwnd, title);
        if(g_lblOnlineUser) {
            swprintf(account, 192, L"当前账号: %ls", user);
            SetWindowTextW(g_lblOnlineUser, account);
        }
        set_status(L"联机模式：连接房间服，创建/加入房间后开始对局");
        break;
    }
    }
}

static void refresh_list_ui(void)
{
    if(!g_list) return;
    uint32_t keep_id = g_selected_room;
    int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    if(sel >= 0 && sel < (int)g_rooms.size())
        keep_id = g_rooms[sel].id;

    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    if(g_rooms.empty()) {
        SendMessageW(g_list, LB_ADDSTRING, 0,
                     (LPARAM)L"(暂无房间 — 请点「创建房间」，或等房主建好后点「刷新」)");
        g_selected_room = 0;
        return;
    }
    int select_idx = 0;
    for(size_t i = 0; i < g_rooms.size(); ++i) {
        wchar_t line[512], name[128], map[256];
        utf8_to_wide(g_rooms[i].name, name, 128);
        utf8_to_wide(g_rooms[i].map_path, map, 256);
        swprintf(line, 512, L"#%u  %ls  %d/%d  %ls  map=%ls",
                 g_rooms[i].id, name,
                 g_rooms[i].player_count, g_rooms[i].max_players,
                 state_name(g_rooms[i].state), map);
        int idx = (int)SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)line);
        /* Store room id in item data (also resolve via g_rooms[index]). */
        SendMessageW(g_list, LB_SETITEMDATA, idx, (LPARAM)(intptr_t)g_rooms[i].id);
        if(keep_id && g_rooms[i].id == keep_id)
            select_idx = idx;
    }
    SendMessageW(g_list, LB_SETCURSEL, select_idx, 0);
    g_selected_room = g_rooms[select_idx].id;
}

static void on_room_list(const struct pf_room_info *rooms, int n, void *ud)
{
    (void)ud;
    if(n == 0 && !g_rooms.empty()) {
        ULONGLONG since = GetTickCount64() - g_last_lobby_push_ms;
        if(g_last_lobby_push_ms && since < 3000) {
            append_log(L"房间列表: 0 个（忽略过期空列表，保留大厅推送）");
            set_status(L"房间列表可能过期 — 保留已推送房间，可点「刷新」");
            return;
        }
    }
    g_rooms.assign(rooms, rooms + n);
    refresh_list_ui();
    wchar_t msg[64];
    swprintf(msg, 64, L"房间列表: %d 个", n);
    set_status(msg);
    append_log(msg);
}

static void on_room(const struct pf_room_info *room, int err, void *ud)
{
    (void)ud;
    /* err == -1: lobby-wide room notify (not an error; we are not necessarily in it) */
    if(err > 0 || !room) {
        wchar_t msg[128];
        swprintf(msg, 128, L"房间操作失败 err=%d (%ls)", err, err_msg(err));
        set_status(msg);
        append_log(msg);
        if(err == 2 || err == 5) {
            /* Stale / already started — drop local entry and refresh. */
            if(g_selected_room) {
                for(auto it = g_rooms.begin(); it != g_rooms.end(); ++it) {
                    if(it->id == g_selected_room) { g_rooms.erase(it); break; }
                }
                g_selected_room = 0;
                refresh_list_ui();
            }
            if(g_room_authed)
                RoomClient_ListRooms();
            append_log(err == 2
                ? L"已请求刷新房间列表（房间不存在）"
                : L"已请求刷新房间列表（房间不可加入/已开局）");
        }
        return;
    }
    bool found = false;
    for(auto &r : g_rooms) {
        if(r.id == room->id) { r = *room; found = true; break; }
    }
    if(!found)
        g_rooms.push_back(*room);
    if(err == -1)
        g_last_lobby_push_ms = GetTickCount64();
    refresh_list_ui();

    wchar_t name[128], msg[256];
    utf8_to_wide(room->name, name, 128);
    int my_slot = -1;
    for(int i = 0; i < room->player_count && i < PF_OC_MAX_PLAYERS; ++i) {
        if(room->players[i].uid == LoginClient_Uid()) {
            my_slot = room->players[i].slot;
            break;
        }
    }
    if(my_slot >= 0 || err == 0) {
        /* Create/join success, or we are a member via update */
        g_selected_room = room->id;
        if(my_slot >= 0) {
            swprintf(msg, 256, L"已进入游戏房间 #%u「%ls」 人数 %d/%d  我的座位=%d",
                     room->id, name, room->player_count, room->max_players, my_slot);
        } else {
            swprintf(msg, 256, L"房间 #%u「%ls」 人数 %d/%d",
                     room->id, name, room->player_count, room->max_players);
        }
        set_status(msg);
        append_log(msg);
    } else {
        /* Lobby notify of someone else's room */
        if(room->state == 0) { /* ROOM_OPEN */
            g_selected_room = room->id;
            swprintf(msg, 256, L"大厅更新: #%u「%ls」 %d/%d OPEN（已选中，可直接点加入）",
                     room->id, name, room->player_count, room->max_players);
        } else {
            swprintf(msg, 256, L"大厅更新: #%u「%ls」 %d/%d %ls（不可加入）",
                     room->id, name, room->player_count, room->max_players,
                     state_name(room->state));
            /* Keep list accurate but don't offer join on started rooms. */
            if(g_selected_room == room->id)
                g_selected_room = 0;
        }
        set_status(msg);
        append_log(msg);
    }
}

static void on_deleted(uint32_t room_id, int reason, void *ud)
{
    (void)reason; (void)ud;
    for(auto it = g_rooms.begin(); it != g_rooms.end(); ++it) {
        if(it->id == room_id) { g_rooms.erase(it); break; }
    }
    refresh_list_ui();
    wchar_t msg[64];
    swprintf(msg, 64, L"房间 #%u 已删除", room_id);
    set_status(msg);
    append_log(msg);
}

static uint32_t selected_room_id(void)
{
    if(!g_list)
        return g_selected_room ? g_selected_room : RoomClient_CurrentRoomId();

    int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    /* Prefer g_rooms[index] — LB_GETITEMDATA can be wrong/stale after refresh. */
    if(sel >= 0 && sel < (int)g_rooms.size()) {
        g_selected_room = g_rooms[sel].id;
        return g_selected_room;
    }
    if(g_rooms.size() == 1) {
        g_selected_room = g_rooms[0].id;
        return g_selected_room;
    }
    if(g_selected_room) {
        for(auto &r : g_rooms) {
            if(r.id == g_selected_room)
                return g_selected_room;
        }
    }
    return RoomClient_CurrentRoomId();
}

static void do_login_flow(bool is_reg)
{
    char host[128], user[64], pass[64];
    wide_to_utf8(g_host, host, sizeof(host));
    wide_to_utf8(g_user, user, sizeof(user));
    wide_to_utf8(g_pass, pass, sizeof(pass));
    int port = get_port(g_loginPort, 27000);

    if(!user[0] || !pass[0]) {
        set_status(L"请填写用户名和密码");
        return;
    }
    if(!LoginClient_Connect(host, (uint16_t)port)) {
        set_status(L"连接登录服失败（请先启动 login_server）");
        append_log(L"连接登录服失败");
        return;
    }
    if(is_reg) {
        if(!LoginClient_Register(user, pass)) {
            set_status(L"发送注册失败");
            return;
        }
        set_status(L"已发送注册...");
        append_log(L"reg_req 已发送");
    } else {
        if(!LoginClient_Login(user, pass)) {
            set_status(L"发送登录失败");
            return;
        }
        set_status(L"已发送登录...");
        append_log(L"login_req 已发送");
    }
}

static void do_logout(void)
{
    RoomClient_LeaveMatch();
    RoomClient_Disconnect();
    LoginClient_Disconnect();
    g_logged_in = false;
    g_room_authed = false;
    g_pending_create = false;
    g_in_match_ui = false;
    g_match_starting = false;
    g_rooms.clear();
    refresh_list_ui();
    refresh_match_peers_ui();
    update_start_buttons();
    show_screen(SCREEN_LOGIN);
    set_status(L"已退出登录");
    append_log(L"logout");
}

static void update_start_buttons(void)
{
    bool eng = EngineLaunch_IsRunning();
    bool in_match = g_in_match_ui || RoomClient_InMatch() || g_match_starting;

    if(g_btnSoloStart) {
        bool busy = eng;
        EnableWindow(g_btnSoloStart, !busy);
        SetWindowTextW(g_btnSoloStart, busy ? L"已进入游戏" : L"开始游戏");
    }
    if(g_btnStartMatch) {
        bool busy = in_match || eng;
        EnableWindow(g_btnStartMatch, !busy);
        SetWindowTextW(g_btnStartMatch, busy ? L"已进入游戏" : L"开始对局");
    }
}

static void do_solo_start(void)
{
    char map[256];
    wide_to_utf8(g_soloMap, map, sizeof(map));
    if(!map[0])
        strncpy(map, "assets/maps/demo.pfmap", sizeof(map) - 1);

    wchar_t eng_err[512];
    if(EngineLaunch_IsRunning()) {
        set_status(L"引擎已在运行中");
        update_start_buttons();
        return;
    }
    /* Disable immediately to block double-click */
    if(g_btnSoloStart) {
        EnableWindow(g_btnSoloStart, FALSE);
        SetWindowTextW(g_btnSoloStart, L"已进入游戏");
    }
    const char *solo_user = LoginClient_Username();
    if(!solo_user || !solo_user[0])
        solo_user = "Player";
    char solo_roster[64];
    snprintf(solo_roster, sizeof(solo_roster), "0:%s", solo_user);
    struct engine_launch_opts opts{};
    opts.map_path = map;
    opts.slot = 0;
    opts.local_user = solo_user;
    opts.roster = solo_roster;
    opts.rng_seed = 1;
    opts.num_players = 1;
    opts.room_id = 0;
    opts.mp_host = NULL;
    if(EngineLaunch_StartMatch(&opts, eng_err, 512)) {
        set_status(L"单机引擎已启动 — 请看 Permafrost Engine 窗口");
        append_log(L"单机模式：已启动 pf.exe / match_online.py");
    } else {
        wchar_t fail[640];
        swprintf(fail, 640, L"引擎启动失败: %ls", eng_err);
        set_status(fail);
        append_log(fail);
    }
    update_start_buttons();
}

static void paint_map_view(void)
{
    if(!g_mapView || g_screen != SCREEN_ONLINE) return;
    RECT rc;
    GetClientRect(g_mapView, &rc);
    HDC hdc = GetDC(g_mapView);
    struct map_preview_marker marks[PF_OC_MAX_PLAYERS];
    int nm = 0;
    struct pf_match_peer peers[PF_OC_MAX_PLAYERS];
    int n = PF_OC_MAX_PLAYERS;
    if(RoomClient_InMatch())
        RoomClient_MatchGetPeers(peers, &n);
    else
        n = 0;
    static const COLORREF cols[] = {
        RGB(255, 80, 80), RGB(80, 160, 255), RGB(255, 220, 60), RGB(80, 220, 120),
        RGB(220, 80, 220), RGB(255, 160, 60), RGB(160, 255, 255), RGB(200, 200, 200)
    };
    static wchar_t labels[PF_OC_MAX_PLAYERS][64];
    for(int i = 0; i < n && nm < PF_OC_MAX_PLAYERS; ++i) {
        if(!peers[i].in_game) continue;
        float ang = (float)peers[i].slot * 6.2831853f / 8.0f;
        marks[nm].u = 0.5f + 0.28f * cosf(ang);
        marks[nm].v = 0.5f + 0.28f * sinf(ang);
        marks[nm].color = cols[peers[i].slot & 7];
        wchar_t uname[32];
        utf8_to_wide(peers[i].username, uname, 32);
        swprintf(labels[nm], 64, L"P%d %ls", peers[i].slot, uname);
        marks[nm].label = labels[nm];
        nm++;
    }
    MapPreview_Draw(hdc, rc, marks, nm);
    ReleaseDC(g_mapView, hdc);
}

static void refresh_match_peers_ui(void)
{
    if(!g_matchList) return;
    SendMessageW(g_matchList, LB_RESETCONTENT, 0, 0);
    struct pf_match_peer peers[PF_OC_MAX_PLAYERS];
    int n = PF_OC_MAX_PLAYERS;
    RoomClient_MatchGetPeers(peers, &n);
    for(int i = 0; i < n; ++i) {
        wchar_t line[256], name[64];
        utf8_to_wide(peers[i].username, name, 64);
        swprintf(line, 256, L"座位%d  %ls  %ls  tick=%u%s",
                 peers[i].slot, name,
                 peers[i].in_game ? L"[已进入游戏]" : L"[等待进入]",
                 peers[i].last_tick,
                 peers[i].slot == RoomClient_LocalSlot() ? L"  <<我" : L"");
        SendMessageW(g_matchList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    if(n == 0)
        SendMessageW(g_matchList, LB_ADDSTRING, 0, (LPARAM)L"(尚暂无对局玩家)");
}

static bool begin_match_session(void)
{
    char host[128];
    wide_to_utf8(g_host, host, sizeof(host));
    if(!host[0]) strncpy(host, "127.0.0.1", sizeof(host) - 1);
    if(!RoomClient_EnterMatch(host)) {
        set_status(L"进入对局失败（UDP/鉴权）");
        append_log(L"EnterMatch 失败");
        return false;
    }
    g_in_match_ui = true;
    struct pf_match_info mi{};
    RoomClient_GetMatchInfo(&mi);
    wchar_t map[256], msg[512];
    utf8_to_wide(mi.map_path, map, 256);
    const char *mpath = mi.map_path[0] ? mi.map_path : "assets/maps/demo.pfmap";
    if(MapPreview_Load(mpath)) {
        swprintf(msg, 512, L"已进入对局并加载地图预览 %ls", map);
        append_log(L"地图预览加载成功");
    } else if(MapPreview_Load("assets/maps/demo.pfmap") ||
              MapPreview_Load("../assets/maps/demo.pfmap")) {
        swprintf(msg, 512, L"已进入对局 — 使用 demo.pfmap 预览");
        append_log(L"地图路径未找到，已回退 demo.pfmap");
    } else {
        swprintf(msg, 512, L"已进入对局 room=#%u 座位=%d — 地图预览失败",
                 mi.room_id, mi.local_slot);
        append_log(L"地图预览加载失败");
    }
    set_status(msg);
    append_log(msg);
    refresh_match_peers_ui();
    if(g_mapView)
        InvalidateRect(g_mapView, NULL, FALSE);
    paint_map_view();

    wchar_t eng_err[512];
    const char *launch_map = mi.map_path[0] ? mi.map_path : mpath;
    const char *launch_user = LoginClient_Username();
    if(!launch_user || !launch_user[0])
        launch_user = "Player";
    char roster[512];
    roster[0] = 0;
    for(int i = 0; i < mi.player_count && i < PF_OC_MAX_PLAYERS; ++i) {
        char piece[80];
        const char *un = mi.players[i].username[0] ? mi.players[i].username : "Player";
        snprintf(piece, sizeof(piece), "%s%d:%s",
                 roster[0] ? ";" : "", mi.players[i].slot, un);
        if(strlen(roster) + strlen(piece) < sizeof(roster) - 1)
            strcat(roster, piece);
    }
    if(!roster[0])
        snprintf(roster, sizeof(roster), "%d:%s", mi.local_slot, launch_user);
    struct engine_launch_opts opts{};
    opts.map_path = launch_map;
    opts.slot = mi.local_slot;
    opts.local_user = launch_user;
    opts.roster = roster;
    opts.rng_seed = mi.rng_seed ? mi.rng_seed : 0xC0FFEEu;
    opts.num_players = mi.player_count > 0 ? mi.player_count : 1;
    opts.room_id = mi.room_id;
    opts.mp_host = "127.0.0.1"; /* same-machine dual client; LAN later */
    if(EngineLaunch_StartMatch(&opts, eng_err, 512)) {
        wchar_t msg[256];
        swprintf(msg, 256, L"已启动引擎（联机锁步 players=%d seed=%llu slot=%d）",
                 opts.num_players, (unsigned long long)opts.rng_seed, opts.slot);
        append_log(msg);
        set_status(L"对局中 — 两端应进入同一张地图并同步指令");
    } else {
        wchar_t fail[640];
        swprintf(fail, 640, L"引擎启动失败: %ls", eng_err);
        append_log(fail);
    }
    g_match_starting = false;
    update_start_buttons();
    return true;
}

static void do_create_room(void)
{
    if(!g_logged_in || !LoginClient_Token()[0]) {
        set_status(L"请先登录，再创建房间");
        return;
    }
    if(!g_room_authed) {
        g_pending_create = true;
        set_status(L"尚未连房间服 — 正在连接...");
        enter_room_server();
        return;
    }
    char name[64], map[256];
    wide_to_utf8(g_roomName, name, sizeof(name));
    wide_to_utf8(g_roomMap, map, sizeof(map));
    if(!RoomClient_CreateRoom(name, map, 8)) {
        set_status(L"发送 create_room 失败");
        g_room_authed = false;
        g_pending_create = true;
        enter_room_server();
        return;
    }
    append_log(L"create_room 已发送");
    set_status(L"创建房间请求已发送...");
}

static void enter_room_server(void)
{
    if(!g_logged_in || !LoginClient_Token()[0]) {
        set_status(L"请先登录");
        return;
    }
    /* Already on room server: just refresh list — reconnect would drop us and
     * delete empty rooms we owned. */
    if(g_room_authed) {
        RoomClient_ListRooms();
        append_log(L"已在房间服 — 刷新列表");
        set_status(L"已连接房间服 — 已刷新房间列表");
        return;
    }
    char host[128];
    wide_to_utf8(g_host, host, sizeof(host));
    if(!host[0])
        strncpy(host, "127.0.0.1", sizeof(host) - 1);
    int port = get_port(g_roomPort, 27001);
    g_room_authed = false;
    if(!RoomClient_Connect(host, (uint16_t)port)) {
        wchar_t msg[256], whost[128];
        utf8_to_wide(host, whost, 128);
        swprintf(msg, 256, L"连接房间服失败 %ls:%d", whost, port);
        set_status(msg);
        append_log(msg);
        return;
    }
    RoomClient_SetCallbacks(on_room_list, on_room, on_deleted, nullptr);
    if(!RoomClient_Auth(LoginClient_Token(), LoginClient_Uid(), LoginClient_Username())) {
        set_status(L"发送 auth 失败");
        return;
    }
    set_status(L"已连接房间服，鉴权中...");
    append_log(L"room auth_req 已发送");

    for(int i = 0; i < 30; ++i) {
        RoomClient_Service();
        bool ok = false; int err = 0;
        if(RoomClient_TakeAuthResult(&ok, &err)) {
            if(ok) {
                g_room_authed = true;
                set_status(L"房间服鉴权成功 — 可创建/加入房间");
                append_log(L"房间服鉴权成功");
                RoomClient_ListRooms();
                if(g_pending_create) {
                    g_pending_create = false;
                    do_create_room();
                }
            } else {
                wchar_t msg[128];
                swprintf(msg, 128, L"房间服鉴权失败 err=%d (%ls)", err, err_msg(err));
                set_status(msg);
                append_log(msg);
                g_pending_create = false;
            }
            return;
        }
        Sleep(20);
    }
    append_log(L"房间服鉴权仍在等待响应...");
}

static HWND make_label(HWND parent, int x, int y, int w, int h, const wchar_t *text)
{
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                         x, y, w, h, parent, NULL, GetModuleHandleW(NULL), NULL);
}

static HWND make_edit(HWND parent, int id, int x, int y, int w, int h, const wchar_t *text, bool pass)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    if(pass) style |= ES_PASSWORD;
    return CreateWindowW(L"EDIT", text, style, x, y, w, h, parent, (HMENU)(intptr_t)id,
                           GetModuleHandleW(NULL), NULL);
}

static HWND make_btn(HWND parent, int id, int x, int y, int w, int h, const wchar_t *text)
{
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, parent, (HMENU)(intptr_t)id, GetModuleHandleW(NULL), NULL);
}

static void track(std::vector<HWND> &g, HWND h)
{
    if(h) g.push_back(h);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch(msg) {
    case WM_CREATE: {
        /* ---- Login screen (centered card) ---- */
        g_lblLoginTitle = make_label(hwnd, 180, 80, 280, 28, L"Permafrost Engine");
        HFONT titleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SendMessageW(g_lblLoginTitle, WM_SETFONT, (WPARAM)titleFont, TRUE);
        track(g_ctrls_login, g_lblLoginTitle);
        track(g_ctrls_login, make_label(hwnd, 180, 118, 280, 22, L"登录后选择单机或联机"));

        track(g_ctrls_login, make_label(hwnd, 160, 170, 50, 22, L"服务器"));
        g_host = make_edit(hwnd, ID_HOST, 220, 168, 140, 24, L"127.0.0.1", false);
        track(g_ctrls_login, g_host);
        track(g_ctrls_login, make_label(hwnd, 370, 170, 40, 22, L"端口"));
        g_loginPort = make_edit(hwnd, ID_LOGIN_PORT, 410, 168, 60, 24, L"27000", false);
        track(g_ctrls_login, g_loginPort);

        track(g_ctrls_login, make_label(hwnd, 160, 210, 50, 22, L"用户"));
        g_user = make_edit(hwnd, ID_USER, 220, 208, 250, 24, L"player1", false);
        track(g_ctrls_login, g_user);
        track(g_ctrls_login, make_label(hwnd, 160, 250, 50, 22, L"密码"));
        g_pass = make_edit(hwnd, ID_PASS, 220, 248, 250, 24, L"123456", true);
        track(g_ctrls_login, g_pass);

        track(g_ctrls_login, make_btn(hwnd, ID_BTN_LOGIN, 220, 300, 120, 36, L"登  录"));
        track(g_ctrls_login, make_btn(hwnd, ID_BTN_REG, 350, 300, 120, 36, L"注  册"));

        /* ---- Mode select ---- */
        g_lblModeWelcome = make_label(hwnd, 80, 100, 480, 28, L"请选择模式");
        SendMessageW(g_lblModeWelcome, WM_SETFONT, (WPARAM)titleFont, TRUE);
        track(g_ctrls_mode, g_lblModeWelcome);
        track(g_ctrls_mode, make_label(hwnd, 120, 160, 400, 22,
                                       L"单机：本地直接开图；联机：房间大厅多人开局"));
        track(g_ctrls_mode, make_btn(hwnd, ID_BTN_SOLO, 160, 220, 280, 48, L"单机模式"));
        track(g_ctrls_mode, make_btn(hwnd, ID_BTN_ONLINE, 160, 290, 280, 48, L"联机模式"));
        track(g_ctrls_mode, make_btn(hwnd, ID_BTN_LOGOUT, 220, 370, 160, 32, L"退出登录"));

        /* ---- Solo ---- */
        track(g_ctrls_solo, make_label(hwnd, 40, 60, 200, 24, L"单机模式"));
        track(g_ctrls_solo, make_label(hwnd, 40, 110, 60, 22, L"地图"));
        g_soloMap = make_edit(hwnd, ID_SOLO_MAP, 100, 108, 420, 24,
                              L"assets/maps/demo.pfmap", false);
        track(g_ctrls_solo, g_soloMap);
        g_btnSoloStart = make_btn(hwnd, ID_BTN_SOLO_START, 100, 160, 160, 40, L"开始游戏");
        track(g_ctrls_solo, g_btnSoloStart);
        track(g_ctrls_solo, make_btn(hwnd, ID_BTN_SOLO_BACK, 280, 160, 120, 40, L"返回"));
        track(g_ctrls_solo, make_label(hwnd, 40, 230, 500, 40,
            L"将启动 lib/pf.exe + scripts/rts/match_online.py（无需房间服）"));

        /* ---- Online lobby (same functionality as before) ---- */
        g_lblOnlineUser = make_label(hwnd, 260, 8, 250, 22, L"当前账号: -");
        track(g_ctrls_online, g_lblOnlineUser);
        track(g_ctrls_online, make_label(hwnd, 10, 10, 70, 22, L"RoomPort"));
        g_roomPort = make_edit(hwnd, ID_ROOM_PORT, 85, 8, 55, 22, L"27001", false);
        track(g_ctrls_online, g_roomPort);
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_ENTER_ROOM, 150, 6, 100, 26, L"连房间服"));
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_ONLINE_BACK, 520, 6, 100, 26, L"返回模式"));

        int y = 42;
        track(g_ctrls_online, make_label(hwnd, 10, y, 40, 22, L"房名"));
        g_roomName = make_edit(hwnd, ID_ROOM_NAME, 55, y, 120, 22, L"demo", false);
        track(g_ctrls_online, g_roomName);
        track(g_ctrls_online, make_label(hwnd, 185, y, 40, 22, L"地图"));
        g_roomMap = make_edit(hwnd, ID_ROOM_MAP, 230, y, 200, 22, L"assets/maps/demo.pfmap", false);
        track(g_ctrls_online, g_roomMap);
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_CREATE, 440, y, 70, 24, L"创建房间"));
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_REFRESH, 520, y, 70, 24, L"刷新"));
        y += 36;
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_JOIN, 10, y, 90, 24, L"加入房间"));
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_DELETE, 110, y, 70, 24, L"删除"));
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_LEAVE, 190, y, 70, 24, L"离开"));
        g_btnStartMatch = make_btn(hwnd, ID_BTN_START, 270, y, 90, 24, L"开始对局");
        track(g_ctrls_online, g_btnStartMatch);
        track(g_ctrls_online, make_btn(hwnd, ID_BTN_LEAVE_MATCH, 370, y, 100, 24, L"退出对局"));
        y += 32;
        track(g_ctrls_online, make_label(hwnd, 10, y, 100, 20, L"房间列表"));
        track(g_ctrls_online, make_label(hwnd, 340, y, 160, 20, L"对局玩家"));
        y += 22;
        g_list = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                               10, y, 310, 140, hwnd, (HMENU)(intptr_t)ID_LIST, GetModuleHandleW(NULL), NULL);
        track(g_ctrls_online, g_list);
        g_matchList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
                               330, y, 290, 140, hwnd, (HMENU)(intptr_t)ID_MATCH_LIST, GetModuleHandleW(NULL), NULL);
        track(g_ctrls_online, g_matchList);
        y += 150;
        track(g_ctrls_online, make_label(hwnd, 10, y, 260, 20, L"地图预览"));
        y += 22;
        g_mapView = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                               10, y, 610, 220, hwnd, (HMENU)(intptr_t)ID_MAP_VIEW, GetModuleHandleW(NULL), NULL);
        track(g_ctrls_online, g_mapView);

        /* Shared bottom status + log */
        g_status = make_label(hwnd, 10, 620, 620, 22, L"请登录");
        g_log = CreateWindowW(L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                              10, 650, 620, 120, hwnd, (HMENU)(intptr_t)ID_LOG, GetModuleHandleW(NULL), NULL);

        SetTimer(hwnd, TIMER_PUMP, 50, NULL);
        append_log(L"Permafrost Client");
        append_log(L"流程: 登录 → 单机 / 联机");
        show_screen(SCREEN_LOGIN);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        /* Login */
        if(id == ID_BTN_REG) { do_login_flow(true); return 0; }
        if(id == ID_BTN_LOGIN) { do_login_flow(false); return 0; }
        /* Mode */
        if(id == ID_BTN_SOLO) {
            show_screen(SCREEN_SOLO);
            return 0;
        }
        if(id == ID_BTN_ONLINE) {
            show_screen(SCREEN_ONLINE);
            enter_room_server();
            return 0;
        }
        if(id == ID_BTN_LOGOUT) { do_logout(); return 0; }
        /* Solo */
        if(id == ID_BTN_SOLO_START) { do_solo_start(); return 0; }
        if(id == ID_BTN_SOLO_BACK) { show_screen(SCREEN_MODE); return 0; }
        /* Online */
        if(id == ID_BTN_ONLINE_BACK) {
            show_screen(SCREEN_MODE);
            return 0;
        }
        if(id == ID_BTN_ENTER_ROOM) { enter_room_server(); return 0; }
        if(id == ID_BTN_CREATE) { do_create_room(); return 0; }
        if(id == ID_BTN_REFRESH) {
            if(!g_room_authed) {
                if(g_logged_in) enter_room_server();
                else set_status(L"请先登录");
                return 0;
            }
            RoomClient_ListRooms();
            append_log(L"list_rooms 已发送");
            return 0;
        }
        if(id == ID_BTN_JOIN) {
            if(!g_room_authed) {
                if(g_logged_in) enter_room_server();
                return 0;
            }
            uint32_t rid = selected_room_id();
            if(!rid) { set_status(L"请先在房间列表中点选一个房间"); return 0; }
            if(RoomClient_CurrentRoomId() == rid) {
                set_status(L"已在该房间");
                return 0;
            }
            if(RoomClient_CurrentRoomId()) {
                set_status(L"请先离开当前房间");
                return 0;
            }
            for(auto &r : g_rooms) {
                if(r.id == rid && r.state != 0) {
                    set_status(L"该房间已开局或未开放，无法加入");
                    append_log(L"join 取消：房间非 OPEN");
                    return 0;
                }
            }
            wchar_t jmsg[96];
            swprintf(jmsg, 96, L"join_room 已发送 room_id=%u", rid);
            append_log(jmsg);
            if(!RoomClient_JoinRoom(rid)) {
                set_status(L"发送 join_room 失败（连接可能断开）");
                g_room_authed = false;
            }
            return 0;
        }
        if(id == ID_BTN_DELETE) {
            uint32_t rid = selected_room_id();
            if(!rid) { set_status(L"请先选中房间"); return 0; }
            RoomClient_DeleteRoom(rid);
            return 0;
        }
        if(id == ID_BTN_LEAVE) {
            uint32_t rid = RoomClient_CurrentRoomId();
            if(!rid) rid = selected_room_id();
            if(!rid) { set_status(L"当前不在房间内"); return 0; }
            RoomClient_LeaveRoom(rid);
            return 0;
        }
        if(id == ID_BTN_START) {
            if(!g_logged_in || !g_room_authed || !RoomClient_CurrentRoomId()) {
                set_status(L"请先进入游戏房间再开始对局");
                return 0;
            }
            if(RoomClient_InMatch() || g_in_match_ui || g_match_starting || EngineLaunch_IsRunning()) {
                set_status(L"已在对局中");
                update_start_buttons();
                return 0;
            }
            {
                struct pf_room_info cur{};
                int nplayers = 0;
                if(RoomClient_GetCurrentRoom(&cur))
                    nplayers = cur.player_count;
                if(nplayers < 2) {
                    set_status(L"至少 2 名玩家在房间内才能开始对局（等对方加入）");
                    append_log(L"match_start 取消：人数不足");
                    return 0;
                }
            }
            g_match_starting = true;
            update_start_buttons();
            RoomClient_StartMatch(0, 10, 2);
            append_log(L"match_start 已发送");
            return 0;
        }
        if(id == ID_BTN_LEAVE_MATCH) {
            RoomClient_LeaveMatch();
            g_in_match_ui = false;
            g_match_starting = false;
            refresh_match_peers_ui();
            paint_map_view();
            update_start_buttons();
            set_status(L"已退出对局会话");
            return 0;
        }
        return 0;
    }
    case WM_TIMER:
        if(wp == TIMER_PUMP) {
            LoginClient_Service();
            RoomClient_Service();

            bool ok; int err;
            if(LoginClient_TakeRegResult(&ok, &err)) {
                wchar_t msg[256];
                if(ok)
                    swprintf(msg, 256, L"注册成功，请点「登录」");
                else
                    swprintf(msg, 256, L"注册失败 err=%d (%ls)", err, err_msg(err));
                set_status(msg);
                append_log(msg);
            }
            if(LoginClient_TakeLoginResult(&ok, &err)) {
                wchar_t msg[256], user[64];
                if(ok) {
                    g_logged_in = true;
                    utf8_to_wide(LoginClient_Username(), user, 64);
                    swprintf(msg, 256, L"登录成功 uid=%lld user=%ls",
                             (long long)LoginClient_Uid(), user);
                    set_status(msg);
                    append_log(msg);
                    show_screen(SCREEN_MODE);
                } else {
                    swprintf(msg, 256, L"登录失败 err=%d (%ls)", err, err_msg(err));
                    set_status(msg);
                    append_log(msg);
                }
            }
            if(RoomClient_TakeAuthResult(&ok, &err)) {
                wchar_t msg[256];
                if(ok) {
                    g_room_authed = true;
                    swprintf(msg, 256, L"房间服鉴权成功");
                    set_status(msg);
                    append_log(msg);
                    RoomClient_ListRooms();
                    if(g_pending_create) {
                        g_pending_create = false;
                        do_create_room();
                    }
                } else {
                    swprintf(msg, 256, L"房间服鉴权失败 err=%d (%ls)", err, err_msg(err));
                    set_status(msg);
                    append_log(msg);
                    g_pending_create = false;
                }
            }

            static uint32_t s_prev_rid = 0;
            uint32_t rid_now = RoomClient_CurrentRoomId();
            if(s_prev_rid != 0 && rid_now == 0 && g_room_authed) {
                set_status(L"已离开游戏房间");
                append_log(L"已离开游戏房间");
            }
            s_prev_rid = rid_now;

            if(RoomClient_TakeMatchStarted()) {
                int slot = RoomClient_LocalSlot();
                if(slot < 0) {
                    wchar_t msg[256];
                    int e = -slot - 1;
                    swprintf(msg, 256, L"开始对局失败 err=%d (%ls)", e, err_msg(e));
                    set_status(msg);
                    append_log(msg);
                    g_match_starting = false;
                    update_start_buttons();
                } else {
                    append_log(L"收到 match_start_ntf — 进入对局...");
                    begin_match_session();
                }
            }

            /* Re-enable start buttons once engine process exits */
            static bool s_eng_was_running = false;
            bool eng_now = EngineLaunch_IsRunning();
            if(s_eng_was_running != eng_now) {
                s_eng_was_running = eng_now;
                if(!eng_now && !RoomClient_InMatch()) {
                    g_in_match_ui = false;
                    g_match_starting = false;
                }
                update_start_buttons();
            }

            if(RoomClient_InMatch() && g_screen == SCREEN_ONLINE) {
                RoomClient_MatchService();
                static uint32_t s_ui_tick = 0;
                uint32_t tk = RoomClient_MatchTick();
                bool peer_evt = RoomClient_MatchPeerEntered();
                if(peer_evt || tk != s_ui_tick) {
                    s_ui_tick = tk;
                    refresh_match_peers_ui();
                    paint_map_view();
                    if(peer_evt)
                        append_log(L"有玩家进入对局");
                    wchar_t st[160];
                    swprintf(st, 160, L"对局中 tick=%u  座位=%d",
                             tk, RoomClient_LocalSlot());
                    set_status(st);
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_PUMP);
        RoomClient_LeaveMatch();
        LoginClient_Disconnect();
        RoomClient_Disconnect();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"PfClientLauncher";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(L"PfClientLauncher", L"Permafrost — 登录",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 660, 820,
                           NULL, NULL, hi, NULL);
    ShowWindow(g_hwnd, show);

    MSG msg;
    while(GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
