/*
 * Symbols normally provided by main.c / script/ / perf.c when linking the
 * full desktop binary. Android PF_NO_PYTHON builds supply no-ops / globals here.
 */
#include "main.h"
#include "script/public/script.h"
#include "perf.h"
#include "lib/public/attr.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/*****************************************************************************/
/* Globals from main.c                                                       */
/*****************************************************************************/

const char *g_basepath = ".";
unsigned long g_frame_idx = 0;
unsigned g_last_frame_ms = 16;
SDL_threadID g_main_thread_id;
SDL_threadID g_render_thread_id;

void pf_android_get_drawable_size(int *out_w, int *out_h);

void Engine_WinDrawableSize(int *out_w, int *out_h)
{
    pf_android_get_drawable_size(out_w, out_h);
}

static int s_pointer_x, s_pointer_y;

void Engine_GetPointer(int *out_x, int *out_y)
{
    if(out_x) *out_x = s_pointer_x;
    if(out_y) *out_y = s_pointer_y;
}

void Engine_SetPointer(int x, int y)
{
    s_pointer_x = x;
    s_pointer_y = y;
}

void Engine_WaitRenderWorkDone(void)
{
}

void Engine_FlushRenderWorkQueue(void)
{
}

void Engine_SetRenderThreadID(SDL_threadID id)
{
    g_render_thread_id = id;
}

void Engine_SwapWindow(void)
{
}

void Engine_ClearPendingEvents(void)
{
}

bool Engine_InRunningState(void)
{
    return true;
}

bool Engine_GetArg(const char *name, size_t maxout, char out[])
{
    (void)name; (void)maxout;
    if(out && maxout)
        out[0] = '\0';
    return false;
}

void pf_android_set_drawable_size(int w, int h);

int Engine_SetRes(int w, int h)
{
    pf_android_set_drawable_size(w, h);
    return 0;
}

void Engine_SetDispMode(enum pf_window_flags wf)
{
    (void)wf;
}

/*****************************************************************************/
/* Script (Python) — disabled under PF_NO_PYTHON                             */
/*****************************************************************************/

bool S_Init(const char *progname, const char *base_path, struct nk_context *ctx)
{
    (void)progname; (void)base_path; (void)ctx;
    return true;
}

void S_Shutdown(void)
{
}

bool S_RunFile(const char *path, int argc, char **argv)
{
    (void)path; (void)argc; (void)argv;
    return false;
}

void S_RunFileAsync(const char *path, int argc, char **argv, struct future *result)
{
    (void)path; (void)argc; (void)argv; (void)result;
}

bool S_GetFilePath(char *out, size_t maxout)
{
    if(out && maxout)
        out[0] = '\0';
    return false;
}

void S_ShowLastError(void)
{
}

void S_RunEventHandler(script_opaque_t callable, script_opaque_t user_arg,
                       void *event_arg)
{
    (void)callable; (void)user_arg; (void)event_arg;
}

void S_Retain(script_opaque_t obj)
{
    (void)obj;
}

void S_Release(script_opaque_t obj)
{
    (void)obj;
}

script_opaque_t S_WrapEngineEventArg(int eventnum, void *arg)
{
    (void)eventnum;
    return arg;
}

script_opaque_t S_UnwrapIfWeakref(script_opaque_t arg)
{
    return arg;
}

bool S_WeakrefDied(script_opaque_t arg)
{
    (void)arg;
    return false;
}

bool S_ObjectsEqual(script_opaque_t a, script_opaque_t b)
{
    return a == b;
}

uint64_t S_ScriptTypeID(uint32_t uid)
{
    (void)uid;
    return 0;
}

int S_FormationPriority(uint32_t uid)
{
    (void)uid;
    return 0;
}

void S_ClearState(void)
{
}

bool S_SaveState(SDL_RWops *stream)
{
    (void)stream;
    return true;
}

bool S_LoadState(SDL_RWops *stream)
{
    (void)stream;
    return true;
}

void S_Task_MaybeExit(void)
{
}

void S_Task_MaybeEnter(void)
{
}

bool S_UI_MouseOverWindow(int mouse_x, int mouse_y)
{
    (void)mouse_x; (void)mouse_y;
    return false;
}

bool S_UI_TextEditHasFocus(void)
{
    return false;
}

void S_Region_NotifyContentsChanged(const char *name)
{
    (void)name;
}

script_opaque_t S_Entity_ObjFromAtts(const char *path, const char *name,
                                     const khash_t(attr) *attr_table,
                                     const vec_attr_t *construct_args)
{
    (void)path; (void)name; (void)attr_table; (void)construct_args;
    return NULL;
}

bool S_Entity_UIDForObj(script_opaque_t obj, uint32_t *out)
{
    (void)obj;
    if(out) *out = 0;
    return false;
}

script_opaque_t S_Entity_ObjForUID(uint32_t uid)
{
    (void)uid;
    return NULL;
}

uint64_t S_Entity_TypeID(script_opaque_t obj)
{
    (void)obj;
    return 0;
}

script_opaque_t S_Region_ObjFromAtts(const char *name, int type, vec2_t pos,
                                     float radius, float xlen, float zlen)
{
    (void)name; (void)type; (void)pos; (void)radius; (void)xlen; (void)zlen;
    return NULL;
}

script_opaque_t S_Camera_ObjFromAtts(const char *name, vec3_t pos,
                                     float pitch, float yaw)
{
    (void)name; (void)pos; (void)pitch; (void)yaw;
    return NULL;
}

void S_Task_KillAll(void)
{
}

/*****************************************************************************/
/* Perf — no-ops                                                             */
/*****************************************************************************/

void Perf_NavParallelReset(void) {}
void Perf_NavParallelAddSince(uint64_t start_ticks) { (void)start_ticks; }
uint32_t Perf_NavParallelGet(void) { return 0; }

void Perf_RecordNavTick(uint32_t dur_us, uint32_t serial_us, uint32_t total_us,
                        uint32_t nwork, uint32_t budget_us)
{
    (void)dur_us; (void)serial_us; (void)total_us; (void)nwork; (void)budget_us;
}

bool Perf_Init(void) { return true; }
void Perf_Shutdown(void) {}
bool Perf_RegisterThread(SDL_threadID tid, const char *name)
{
    (void)tid; (void)name;
    return true;
}
void Perf_BeginTick(void) {}
void Perf_FinishTick(void) {}
void Perf_Push(const char *name) { (void)name; }
void Perf_Pop(const char **out) { if(out) *out = NULL; }
void Perf_PushGPU(const char *name, uint32_t cookie)
{
    (void)name; (void)cookie;
}
void Perf_PopGPU(uint32_t cookie) { (void)cookie; }
bool Perf_IsRoot(void) { return true; }

size_t Perf_Report(size_t maxout, struct perf_info **out)
{
    (void)maxout;
    if(out) *out = NULL;
    return 0;
}

void Perf_GetMemoryStats(struct perf_mem_stats *out)
{
    if(out) memset(out, 0, sizeof(*out));
}
void Perf_GetVramStats(struct vram_stats *out)
{
    if(out) memset(out, 0, sizeof(*out));
}
void Perf_GetGpuFrameStats(struct gpu_frame_stats *out)
{
    if(out) memset(out, 0, sizeof(*out));
}
void Perf_GetMemoryAccounting(struct mem_accounting *out)
{
    (void)out;
}
void Perf_GetGpuMemoryAccounting(struct gpu_mem_accounting *out)
{
    (void)out;
}
uint32_t Perf_LastFrameMS(void) { return 0; }
uint32_t Perf_CurrFrameMS(void) { return 0; }
uint64_t Perf_LastFrameAllocdBytes(void) { return 0; }
size_t Perf_GetNavTickTimes(size_t maxout, struct nav_tick_sample *out)
{
    (void)maxout; (void)out;
    return 0;
}

/*****************************************************************************/
/* nk_file_browser — desktop file picker (not used on Android asset path)    */
/*****************************************************************************/

#include "lib/public/nk_file_browser.h"

struct file *nk_file_list(const char *dir, size_t *out_size)
{
    (void)dir;
    if(out_size) *out_size = 0;
    return NULL;
}

void nk_file_browser(struct nk_context *ctx, struct nk_fb_state *state)
{
    (void)ctx; (void)state;
}

/*****************************************************************************/
/* Net / Lobby — offline stubs when batch < 2 (no src/net linked)            */
/*****************************************************************************/

#if !defined(PF_ANDROID_HAVE_NET)

#include "net/public/net.h"

bool Net_Init(void) { return true; }
void Net_Shutdown(void) {}
bool Net_HostCreate(uint16_t port, int max_peers)
{
    (void)port; (void)max_peers;
    return false;
}
bool Net_ClientCreate(void) { return false; }
void Net_Destroy(void) {}
bool Net_Connect(struct net_address addr)
{
    (void)addr;
    return false;
}
void Net_DisconnectPeer(int peer_slot) { (void)peer_slot; }
void Net_DisconnectAll(void) {}
bool Net_Service(uint32_t timeout_ms, struct net_event *out_event)
{
    (void)timeout_ms; (void)out_event;
    return false;
}
void Net_FreeEvent(struct net_event *ev) { (void)ev; }
bool Net_Send(int peer_slot, enum net_channel ch, const void *data, size_t len)
{
    (void)peer_slot; (void)ch; (void)data; (void)len;
    return false;
}
bool Net_Broadcast(enum net_channel ch, const void *data, size_t len)
{
    (void)ch; (void)data; (void)len;
    return false;
}
int Net_PeerCount(void) { return 0; }
bool Net_GetPeer(int slot, struct net_peer *out)
{
    (void)slot; (void)out;
    return false;
}
bool Net_IsHost(void) { return false; }
uint32_t Net_NowMs(void) { return 0; }
bool Net_ParseAddress(const char *host, uint16_t port, struct net_address *out)
{
    (void)host; (void)port; (void)out;
    return false;
}
void Net_AddressToString(struct net_address addr, char *buf, size_t buflen)
{
    (void)addr;
    if(buf && buflen) buf[0] = '\0';
}

bool Lobby_Host(uint16_t port, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb)
{
    (void)port; (void)local_name; (void)faction_id; (void)cb;
    return false;
}
bool Lobby_Join(struct net_address addr, const char *local_name, int faction_id,
                const struct lobby_callbacks *cb)
{
    (void)addr; (void)local_name; (void)faction_id; (void)cb;
    return false;
}
void Lobby_Shutdown(void) {}
void Lobby_Service(void) {}
bool Lobby_SetReady(bool ready) { (void)ready; return false; }
bool Lobby_SetMap(const char *map_path) { (void)map_path; return false; }
bool Lobby_SetSeed(uint64_t seed) { (void)seed; return false; }
bool Lobby_StartMatch(void) { return false; }
bool Lobby_SendChat(const char *text) { (void)text; return false; }
bool Lobby_IsHost(void) { return false; }
int Lobby_LocalSlot(void) { return 0; }
int Lobby_PlayerCount(void) { return 0; }
bool Lobby_GetPlayers(struct lobby_player *out, int *inout_count)
{
    (void)out;
    if(inout_count) *inout_count = 0;
    return false;
}

#endif /* !PF_ANDROID_HAVE_NET */
