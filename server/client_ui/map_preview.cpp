/*
 * Parse Permafrost .pfmap (tile tokens) and draw a top-down GDI preview.
 * Tile encoding matches src/map/map_asset_load.c m_al_parse_tile.
 */
#include "map_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cstdint>
#include <vector>
#include <string>

#define MP_TPC 32 /* TILES_PER_CHUNK */

struct MpTile {
    int height;
    int mat;
    bool water;
};

static std::vector<MpTile> g_tiles;
static int g_tw, g_th;
static int g_chunks_w, g_chunks_h;
static bool g_ready;

static COLORREF mat_color(int mat, int height)
{
    static const COLORREF base[] = {
        RGB(70, 140, 55),
        RGB(110, 95, 70),
        RGB(120, 120, 120),
        RGB(90, 150, 60),
        RGB(100, 90, 50),
        RGB(130, 100, 60),
        RGB(140, 110, 70),
        RGB(90, 90, 100),
        RGB(200, 210, 220),
        RGB(180, 60, 30),
        RGB(210, 190, 120),
    };
    int n = (int)(sizeof(base) / sizeof(base[0]));
    COLORREF c = base[(mat >= 0 ? mat : 0) % n];
    int shade = height * 6;
    if(shade > 80) shade = 80;
    if(shade < -40) shade = -40;
    int r = GetRValue(c) + shade;
    int g = GetGValue(c) + shade;
    int b = GetBValue(c) + shade / 2;
    if(r < 0) r = 0; if(r > 255) r = 255;
    if(g < 0) g = 0; if(g > 255) g = 255;
    if(b < 0) b = 0; if(b > 255) b = 255;
    return RGB(r, g, b);
}

static bool parse_tile_tok(const char *str, MpTile *out)
{
    if(!str || strlen(str) < 12) return false;
    if(str[2] < '0' || str[2] > '9' || str[3] < '0' || str[3] > '9') return false;
    int sign = (str[1] == '-') ? -1 : 1;
    int h = sign * (10 * (str[2] - '0') + (str[3] - '0'));
    int mat = 100 * (str[6] - '0') + 10 * (str[7] - '0') + (str[8] - '0');
    out->height = h;
    out->mat = mat;
    out->water = (h < 0);
    return true;
}

static bool try_open(const char *path, FILE **fp)
{
    *fp = fopen(path, "rb");
    return *fp != nullptr;
}

static bool resolve_and_open(const char *hint, FILE **fp, char *resolved, size_t rsz)
{
    resolved[0] = 0;
    if(!hint || !hint[0])
        hint = "assets/maps/demo.pfmap";

    const char *base = hint;
    const char *slash = strrchr(hint, '/');
    const char *bslash = strrchr(hint, '\\');
    if(bslash && (!slash || bslash > slash)) slash = bslash;
    if(slash) base = slash + 1;

    char bufs[8][512];
    const char *cands[12];
    int n = 0, bi = 0;
    cands[n++] = hint;
    auto add = [&](const char *fmt, const char *a) {
        if(bi >= 8 || n >= 12) return;
        snprintf(bufs[bi], sizeof(bufs[bi]), fmt, a);
        cands[n++] = bufs[bi++];
    };
    add("../assets/maps/%s", base);
    add("../../assets/maps/%s", base);
    add("assets/maps/%s", base);
    add("D:/study/permafrost-engine-master/assets/maps/%s", base);

    for(int i = 0; i < n; ++i) {
        if(try_open(cands[i], fp)) {
            strncpy(resolved, cands[i], rsz - 1);
            return true;
        }
    }
    return false;
}

bool MapPreview_Load(const char *map_path_hint)
{
    MapPreview_Clear();
    FILE *fp = nullptr;
    char resolved[512];
    if(!resolve_and_open(map_path_hint, &fp, resolved, sizeof(resolved)))
        return false;

    char line[4096];
    float version = 1.0f;
    int num_mats = 0, num_splats = 0, num_rows = 0, num_cols = 0;
    auto fail = [&]() {
        if(fp) fclose(fp);
        MapPreview_Clear();
        return false;
    };

    if(!fgets(line, sizeof(line), fp) || sscanf(line, "version %f", &version) != 1)
        return fail();
    if(!fgets(line, sizeof(line), fp) || sscanf(line, "num_materials %d", &num_mats) != 1)
        return fail();
    if(version >= 1.1f) {
        if(!fgets(line, sizeof(line), fp) || sscanf(line, "num_splats %d", &num_splats) != 1)
            return fail();
    }
    if(!fgets(line, sizeof(line), fp) || sscanf(line, "num_rows %d", &num_rows) != 1)
        return fail();
    if(!fgets(line, sizeof(line), fp) || sscanf(line, "num_cols %d", &num_cols) != 1)
        return fail();

    for(int i = 0; i < num_mats; ++i)
        if(!fgets(line, sizeof(line), fp)) return fail();
    for(int i = 0; i < num_splats; ++i)
        if(!fgets(line, sizeof(line), fp)) return fail();

    g_chunks_w = num_cols;
    g_chunks_h = num_rows;
    g_tw = num_cols * MP_TPC;
    g_th = num_rows * MP_TPC;
    if(g_tw <= 0 || g_th <= 0) return fail();
    g_tiles.assign((size_t)g_tw * (size_t)g_th, MpTile{});

    int nchunks = num_rows * num_cols;
    for(int ci = 0; ci < nchunks; ++ci) {
        int cr = ci / num_cols;
        int cc = ci % num_cols;
        int tiles_needed = MP_TPC * MP_TPC;
        int tiles_got = 0;
        while(tiles_got < tiles_needed) {
            if(!fgets(line, sizeof(line), fp)) return fail();
            char *ctx = nullptr;
            char *tok = strtok_s(line, " \t\r\n", &ctx);
            while(tok && tiles_got < tiles_needed) {
                MpTile t{};
                if(parse_tile_tok(tok, &t)) {
                    int tr = tiles_got / MP_TPC;
                    int tc = tiles_got % MP_TPC;
                    int gx = cc * MP_TPC + tc;
                    int gy = cr * MP_TPC + tr;
                    g_tiles[(size_t)gy * (size_t)g_tw + (size_t)gx] = t;
                    tiles_got++;
                }
                tok = strtok_s(nullptr, " \t\r\n", &ctx);
            }
        }
    }

    fclose(fp);
    g_ready = true;
    return true;
}

void MapPreview_Clear(void)
{
    g_tiles.clear();
    g_tw = g_th = 0;
    g_chunks_w = g_chunks_h = 0;
    g_ready = false;
}

bool MapPreview_Ready(void) { return g_ready; }
int MapPreview_Width(void) { return g_tw; }
int MapPreview_Height(void) { return g_th; }

void MapPreview_Draw(HDC hdc, RECT rc,
                     const struct map_preview_marker *marks, int nmarks)
{
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if(w < 8 || h < 8) return;

    HBRUSH bg = CreateSolidBrush(RGB(20, 28, 24));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    if(!g_ready || g_tw <= 0 || g_th <= 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200, 200, 200));
        DrawTextW(hdc, L"Map not loaded / .pfmap missing", -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_tw;
    bmi.bmiHeader.biHeight = -g_th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if(!bmp || !bits) {
        if(bmp) DeleteObject(bmp);
        DeleteDC(mem);
        return;
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    auto *px = (uint32_t *)bits;
    for(int y = 0; y < g_th; ++y) {
        for(int x = 0; x < g_tw; ++x) {
            const MpTile &t = g_tiles[(size_t)y * (size_t)g_tw + (size_t)x];
            COLORREF c = t.water ? RGB(30, 80, 160) : mat_color(t.mat, t.height);
            px[y * g_tw + x] = (uint32_t)GetRValue(c) | ((uint32_t)GetGValue(c) << 8) |
                               ((uint32_t)GetBValue(c) << 16);
        }
    }

    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, rc.left, rc.top, w, h, mem, 0, 0, g_tw, g_th, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);

    HPEN grid = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(hdc, grid);
    for(int c = 1; c < g_chunks_w; ++c) {
        int x = rc.left + (c * MP_TPC) * w / g_tw;
        MoveToEx(hdc, x, rc.top, NULL);
        LineTo(hdc, x, rc.bottom);
    }
    for(int r = 1; r < g_chunks_h; ++r) {
        int y = rc.top + (r * MP_TPC) * h / g_th;
        MoveToEx(hdc, rc.left, y, NULL);
        LineTo(hdc, rc.right, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(grid);

    SetBkMode(hdc, TRANSPARENT);
    for(int i = 0; i < nmarks; ++i) {
        float u = marks[i].u; if(u < 0) u = 0; if(u > 1) u = 1;
        float v = marks[i].v; if(v < 0) v = 0; if(v > 1) v = 1;
        int cx = rc.left + (int)(u * (w - 1));
        int cy = rc.top + (int)(v * (h - 1));
        HBRUSH br = CreateSolidBrush(marks[i].color);
        HGDIOBJ oldBr = SelectObject(hdc, br);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ oldP = SelectObject(hdc, pen);
        Ellipse(hdc, cx - 7, cy - 7, cx + 7, cy + 7);
        SelectObject(hdc, oldP);
        SelectObject(hdc, oldBr);
        DeleteObject(pen);
        DeleteObject(br);
        if(marks[i].label) {
            SetTextColor(hdc, RGB(255, 255, 220));
            RECT tr{ cx + 8, cy - 8, cx + 140, cy + 12 };
            DrawTextW(hdc, marks[i].label, -1, &tr, DT_LEFT | DT_SINGLELINE);
        }
    }

    SetTextColor(hdc, RGB(240, 240, 240));
    wchar_t title[128];
    swprintf(title, 128, L"Map preview %dx%d tiles", g_tw, g_th);
    RECT tr = rc;
    tr.bottom = tr.top + 22;
    DrawTextW(hdc, title, -1, &tr, DT_LEFT | DT_SINGLELINE);
}
