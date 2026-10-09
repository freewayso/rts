/*
 * Resolve and launch lib/pf.exe with scripts/rts/match_online.py after match_start.
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

#include "engine_launch.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static HANDLE g_proc = NULL;

static bool file_exists_w(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dir_has_engine_markers(const wchar_t *root)
{
    wchar_t p[MAX_PATH];
    swprintf(p, MAX_PATH, L"%ls\\assets\\maps", root);
    DWORD a = GetFileAttributesW(p);
    if(a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    swprintf(p, MAX_PATH, L"%ls\\scripts\\rts\\match_online.py", root);
    if(!file_exists_w(p)) {
        swprintf(p, MAX_PATH, L"%ls\\scripts\\rts\\main.py", root);
        if(!file_exists_w(p))
            return false;
    }
    return true;
}

static void parent_dir(wchar_t *path)
{
    size_t n = wcslen(path);
    while(n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) {
        path[--n] = 0;
    }
    while(n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/') {
        path[--n] = 0;
    }
    while(n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) {
        path[--n] = 0;
    }
}

bool EngineLaunch_FindRoot(wchar_t *out, int out_n)
{
    if(!out || out_n < 8) return false;
    out[0] = 0;

    /* 1) PF_ENGINE_ROOT */
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"PF_ENGINE_ROOT", env, MAX_PATH);
    if(n > 0 && n < MAX_PATH && dir_has_engine_markers(env)) {
        wcsncpy(out, env, out_n - 1);
        out[out_n - 1] = 0;
        return true;
    }

    /* 2) Walk up from this exe (server/build → server → repo) */
    wchar_t exe[MAX_PATH];
    if(!GetModuleFileNameW(NULL, exe, MAX_PATH))
        return false;
    wchar_t cur[MAX_PATH];
    wcsncpy(cur, exe, MAX_PATH - 1);
    cur[MAX_PATH - 1] = 0;
    parent_dir(cur); /* drop exe name → build dir */
    for(int i = 0; i < 6; ++i) {
        if(dir_has_engine_markers(cur)) {
            wcsncpy(out, cur, out_n - 1);
            out[out_n - 1] = 0;
            return true;
        }
        parent_dir(cur);
        if(!cur[0]) break;
    }

    /* 3) Current working directory */
    if(GetCurrentDirectoryW(MAX_PATH, cur) && dir_has_engine_markers(cur)) {
        wcsncpy(out, cur, out_n - 1);
        out[out_n - 1] = 0;
        return true;
    }
    return false;
}

static bool find_pf_exe(const wchar_t *root, wchar_t *exe_out, int exe_n)
{
    static const wchar_t *rels[] = {
        L"lib\\pf.exe",
        L"bin\\pf.exe",
        L"pf.exe",
        NULL
    };
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"PF_ENGINE_EXE", env, MAX_PATH);
    if(n > 0 && n < MAX_PATH && file_exists_w(env)) {
        wcsncpy(exe_out, env, exe_n - 1);
        exe_out[exe_n - 1] = 0;
        return true;
    }
    for(int i = 0; rels[i]; ++i) {
        swprintf(exe_out, exe_n, L"%ls\\%ls", root, rels[i]);
        if(file_exists_w(exe_out))
            return true;
    }
    return false;
}

static void utf8_to_wide(const char *u8, wchar_t *out, int out_n)
{
    MultiByteToWideChar(CP_UTF8, 0, u8 ? u8 : "", -1, out, out_n);
}

bool EngineLaunch_IsRunning(void)
{
    if(!g_proc) return false;
    DWORD code = STILL_ACTIVE;
    if(!GetExitCodeProcess(g_proc, &code) || code != STILL_ACTIVE) {
        CloseHandle(g_proc);
        g_proc = NULL;
        return false;
    }
    return true;
}

/* Keep cmdline / env tokens safe for --NAME=VALUE parsing. */
static void sanitize_token(const char *in, char *out, int out_n)
{
    if(!out || out_n < 2) return;
    int j = 0;
    if(!in) in = "";
    for(int i = 0; in[i] && j < out_n - 1; ++i) {
        unsigned char c = (unsigned char)in[i];
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
            out[j++] = (char)c;
        else if(c == ' ' || c == ':' || c == ';')
            out[j++] = '_';
    }
    if(j == 0) {
        out[0] = 'P';
        out[1] = 0;
        return;
    }
    out[j] = 0;
}

bool EngineLaunch_StartMatch(const struct engine_launch_opts *opts,
                             wchar_t *err_out, int err_n)
{
    if(err_out && err_n > 0) err_out[0] = 0;
    if(!opts) {
        if(err_out && err_n > 0)
            wcsncpy(err_out, L"EngineLaunch opts 为空", err_n - 1);
        return false;
    }

    const char *map_path = opts->map_path;
    int slot = opts->slot;
    const char *local_user = opts->local_user;
    const char *roster = opts->roster;

    if(EngineLaunch_IsRunning()) {
        if(err_out && err_n > 0)
            wcsncpy(err_out, L"引擎已在运行中", err_n - 1);
        return false;
    }

    wchar_t root[MAX_PATH];
    if(!EngineLaunch_FindRoot(root, MAX_PATH)) {
        if(err_out && err_n > 0)
            wcsncpy(err_out,
                    L"找不到引擎根目录（需含 assets/maps 与 scripts/rts）。可设 PF_ENGINE_ROOT",
                    err_n - 1);
        return false;
    }

    wchar_t pfexe[MAX_PATH];
    if(!find_pf_exe(root, pfexe, MAX_PATH)) {
        if(err_out && err_n > 0)
            swprintf(err_out, err_n,
                     L"未找到 pf.exe（已查 %ls\\lib\\pf.exe）。请先: make PLAT=WINDOWS pf  或设 PF_ENGINE_EXE",
                     root);
        return false;
    }

    wchar_t script[MAX_PATH];
    swprintf(script, MAX_PATH, L"%ls\\scripts\\rts\\match_online.py", root);
    if(!file_exists_w(script))
        swprintf(script, MAX_PATH, L"%ls\\scripts\\rts\\main.py", root);
    /* Engine script path helpers historically expect '/', convert for argv. */
    for(wchar_t *p = script; *p; ++p) {
        if(*p == L'\\')
            *p = L'/';
    }

    wchar_t root_fwd[MAX_PATH];
    wcsncpy(root_fwd, root, MAX_PATH - 1);
    root_fwd[MAX_PATH - 1] = 0;
    for(wchar_t *p = root_fwd; *p; ++p) {
        if(*p == L'\\')
            *p = L'/';
    }

    /* Normalize map path for the Python entry */
    char map_utf8[512];
    if(map_path && map_path[0]) {
        strncpy(map_utf8, map_path, sizeof(map_utf8) - 1);
        map_utf8[sizeof(map_utf8) - 1] = 0;
    } else {
        strncpy(map_utf8, "assets/maps/demo.pfmap", sizeof(map_utf8) - 1);
    }
    /* Prefer assets/maps/... form */
    if(strncmp(map_utf8, "maps/", 5) == 0) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "assets/%s", map_utf8);
        strncpy(map_utf8, tmp, sizeof(map_utf8) - 1);
    }

    wchar_t map_w[512];
    utf8_to_wide(map_utf8, map_w, 512);

    /* Build environment block: inherit + PF_MATCH_* */
    wchar_t slot_s[32];
    swprintf(slot_s, 32, L"%d", slot);

    char user_s[64], roster_s[512];
    sanitize_token(local_user && local_user[0] ? local_user : "Player", user_s, sizeof(user_s));
    if(roster && roster[0]) {
        /* Roster already built as slot:name;... — sanitize each name part only */
        int j = 0;
        for(int i = 0; roster[i] && j < (int)sizeof(roster_s) - 1; ++i) {
            unsigned char c = (unsigned char)roster[i];
            if(c == ';' || c == ':' || (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '_' || c == '-' || c == '.')
                roster_s[j++] = (char)c;
            else if(c == ' ')
                roster_s[j++] = '_';
        }
        roster_s[j] = 0;
    } else {
        /* Solo / single-seat fallback */
        snprintf(roster_s, sizeof(roster_s), "%d:%s", slot, user_s);
    }

    wchar_t user_w[64], roster_w[512];
    utf8_to_wide(user_s, user_w, 64);
    utf8_to_wide(roster_s, roster_w, 512);

    /* Lockstep P2P: seat 0 hosts; others join. Port derived from room_id. */
    int num_players = opts->num_players > 0 ? opts->num_players : 1;
    int enable_mp = (num_players >= 2 && opts->mp_host && opts->mp_host[0]);
    char seed_s[32], players_s[16], room_s[16], port_s[16], role_s[8], host_s[64];
    snprintf(seed_s, sizeof(seed_s), "%llu", (unsigned long long)opts->rng_seed);
    snprintf(players_s, sizeof(players_s), "%d", num_players);
    snprintf(room_s, sizeof(room_s), "%u", (unsigned)opts->room_id);
    unsigned mp_port = 27750u + (opts->room_id % 1000u);
    snprintf(port_s, sizeof(port_s), "%u", mp_port);
    strncpy(role_s, (enable_mp && slot == 0) ? "host" : (enable_mp ? "join" : "solo"),
            sizeof(role_s) - 1);
    role_s[sizeof(role_s) - 1] = 0;
    sanitize_token(enable_mp ? opts->mp_host : "", host_s, sizeof(host_s));
    if(!host_s[0])
        strncpy(host_s, "127.0.0.1", sizeof(host_s) - 1);

    wchar_t seed_w[32], players_w[16], room_w[16], port_w[16], role_w[8], host_w[64];
    utf8_to_wide(seed_s, seed_w, 32);
    utf8_to_wide(players_s, players_w, 16);
    utf8_to_wide(room_s, room_w, 16);
    utf8_to_wide(port_s, port_w, 16);
    utf8_to_wide(role_s, role_w, 8);
    utf8_to_wide(host_s, host_w, 64);

    SetEnvironmentVariableW(L"PF_MATCH_MAP", map_w);
    SetEnvironmentVariableW(L"PF_MATCH_SLOT", slot_s);
    SetEnvironmentVariableW(L"PF_MATCH_USER", user_w);
    SetEnvironmentVariableW(L"PF_MATCH_ROSTER", roster_w);
    SetEnvironmentVariableW(L"PF_MATCH_SEED", seed_w);
    SetEnvironmentVariableW(L"PF_MATCH_PLAYERS", players_w);
    SetEnvironmentVariableW(L"PF_MATCH_ROOM", room_w);
    SetEnvironmentVariableW(L"PF_MP_PORT", port_w);
    SetEnvironmentVariableW(L"PF_MP_ROLE", role_w);
    SetEnvironmentVariableW(L"PF_MP_HOST", host_w);

    /* Prepend engine lib/ to PATH so mimalloc/SDL2/python DLLs resolve */
    {
        wchar_t libdir[MAX_PATH], oldpath[32768], newpath[32768 + MAX_PATH];
        swprintf(libdir, MAX_PATH, L"%ls\\lib", root);
        DWORD pn = GetEnvironmentVariableW(L"PATH", oldpath, 32768);
        if(pn == 0 || pn >= 32768)
            oldpath[0] = 0;
        swprintf(newpath, 32768 + MAX_PATH, L"%ls;%ls", libdir, oldpath);
        SetEnvironmentVariableW(L"PATH", newpath);
    }

    /* cmdline: argv[1]=basepath argv[2]=script; Engine_GetArg reads --NAME=VALUE */
    wchar_t cmdline[4096];
    swprintf(cmdline, 4096,
             L"\"%ls\" \"%ls\" \"%ls\" --PF_MATCH_MAP=%ls --PF_MATCH_SLOT=%ls "
             L"--PF_MATCH_USER=%ls --PF_MATCH_ROSTER=%ls "
             L"--PF_MATCH_SEED=%ls --PF_MATCH_PLAYERS=%ls --PF_MATCH_ROOM=%ls "
             L"--PF_MP_PORT=%ls --PF_MP_ROLE=%ls --PF_MP_HOST=%ls",
             pfexe, root_fwd, script, map_w, slot_s, user_w, roster_w,
             seed_w, players_w, room_w, port_w, role_w, host_w);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* Working directory = engine root so relative assets resolve */
    BOOL ok = CreateProcessW(
        pfexe,
        cmdline,
        NULL, NULL,
        FALSE,
        0,
        NULL, /* inherit env (with our SetEnvironmentVariable) */
        root,
        &si, &pi);

    if(!ok) {
        DWORD e = GetLastError();
        if(err_out && err_n > 0)
            swprintf(err_out, err_n, L"CreateProcess 失败 err=%lu  cmdline=%ls", e, cmdline);
        return false;
    }

    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    return true;
}
