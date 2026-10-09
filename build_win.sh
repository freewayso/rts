#!/usr/bin/env bash
set -euo pipefail
export PATH="/mingw64/bin:/usr/bin:$PATH"
cd /d/study/permafrost-engine-master
export GIT_VERSION=nogit
rm -rf deps/GLEW/auto/OpenGL-Registry deps/GLEW/auto/EGL-Registry
rm -rf deps/openal-soft/build deps/mimalloc/build
mkdir -p deps/openal-soft/build lib
make deps PLAT=WINDOWS GIT_VERSION=nogit
make pf PLAT=WINDOWS GIT_VERSION=nogit
make launchers PLAT=WINDOWS GIT_VERSION=nogit || true
ls -la lib
ls -la bin demo editor 2>/dev/null || true
echo BUILD_OK
