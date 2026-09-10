# NOTICE

This repository is a **modified fork** of Permafrost Engine.

It is **not** an official release by the original author. Do not represent it
as an original work.

## This fork (maintainer)

| | |
|---|---|
| Name | freewayso |
| GitHub | https://github.com/freewayso |
| Contact | 719452210@qq.com |

Modifications in this tree (lockstep/KCP, dedicated room server, lobby
directory, Chinese UI wiring, and related hooks) are contributed by the
maintainer above and are released under the same GPLv3 terms as the engine,
with the original linking exception.

## Original project

| | |
|---|---|
| Project | Permafrost Engine |
| Author | Eduard Permyakov |
| Upstream | https://github.com/eduard-permyakov/permafrost-engine |
| Email | edward.permyakov@gmail.com |
| License | GNU GPLv3 + special linking exception (`LICENSE.txt`) |

Copyright (C) 2017-2026 Eduard Permyakov

## Third-party code vendored in this tree

### KCP

- Author: skywind3000 (linyaohui)
- Upstream: https://github.com/skywind3000/kcp
- License: MIT (`LICENSE-KCP.txt`)
- Files: `src/net/ikcp.c`, `src/net/ikcp.h`

### Other bundled dependencies

SDL2, GLEW, Python 2.7, OpenAL-soft, and mimalloc keep their own license
files under `deps/`. Demo models under `assets/models/` keep their
`license.txt` / `README.txt` attribution (often CC-BY).

## Fonts that must not be uploaded

These files are **not** part of the open-source grant and must not be
committed or pushed:

- `assets/fonts/simhei.ttf` (Windows / Microsoft)
- `assets/fonts/HYZhongHeiTi.ttf` (commercial typeface)

Place a redistributable CJK font locally if you need Chinese glyphs.
