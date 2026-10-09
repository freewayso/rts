#pragma once
/* Lightweight top-down .pfmap preview for the Win32 lobby (GDI). */
#ifndef PF_MAP_PREVIEW_H
#define PF_MAP_PREVIEW_H

#include <windows.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool MapPreview_Load(const char *map_path_hint); /* relative or absolute .pfmap */
void MapPreview_Clear(void);
bool MapPreview_Ready(void);
int  MapPreview_Width(void);  /* tiles */
int  MapPreview_Height(void);

/* Draw into hwnd client rect (top-down). players: optional markers */
struct map_preview_marker {
    float u, v; /* 0..1 across map */
    COLORREF color;
    const wchar_t *label; /* optional, may be NULL */
};

void MapPreview_Draw(HDC hdc, RECT rc,
                     const struct map_preview_marker *marks, int nmarks);

#ifdef __cplusplus
}
#endif

#endif
