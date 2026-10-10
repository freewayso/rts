# Android APK / libpermafrost.so 现状

完整说明见 **[android/README.md](../android/README.md)**。

摘要：

- **可以打 APK**：`cd android && gradlew :app:assembleDebug`（默认 `-DPF_ANDROID_LINK_ENGINE=ON`）
- **完整 RTS 已接入**：`android_engine.c` 在 GL 线程上执行 `G_Init` / `R_InitFromCurrentContext` / `G_LoadMap(demo.pfmap)` / `Scene_Load`，每帧 `G_Update` + `G_Render`
- **触摸**：拖动 = 中键平移；点按无选中 = 左键选中；点按有选中 = 右键移动
- **资产**：首次启动把 `maps/`、`models/`、`shaders_es/` 等拷到 `filesDir`
- 桌面完整构建仍是：`make pf PLAT=WINDOWS` → `lib/pf.exe`

后续：GLES 桌面 API 回退质量、OpenAL、联机。