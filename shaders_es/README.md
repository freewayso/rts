# OpenGL ES 3.0 shaders

Auto-converted from `shaders/` (`#version 330 core` → `#version 300 es` + precision).

On Android (`PF_GLES`), the loader prefers `shaders_es/` over `shaders/`.

Notes:
- Geometry shaders are unavailable on ES 3.0 — those programs must be disabled or rewritten.
- Compute requires ES 3.1; default Android quality disables compute paths.
- Water reflection/refraction RTs are disabled at quality 0 / `PF_GLES_WATER_FX=0`.
