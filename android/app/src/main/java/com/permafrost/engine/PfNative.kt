package com.permafrost.engine

object PfNative {
    init {
        System.loadLibrary("permafrost")
    }

    @JvmStatic external fun init(filesDir: String, w: Int, h: Int): Boolean
    @JvmStatic external fun resize(w: Int, h: Int)
    @JvmStatic external fun frame()
    @JvmStatic external fun pause()
    @JvmStatic external fun resume()
    @JvmStatic external fun shutdown()
    @JvmStatic external fun touch(type: Int, x: Float, y: Float, pointerId: Int)
    @JvmStatic external fun setQuality(level: Int)
    @JvmStatic external fun getQuality(): Int

    /* LAN P2P lobby — call only on the GL thread (via GLSurfaceView.queueEvent). */
    @JvmStatic external fun mpHost(port: Int, name: String, faction: Int): Boolean
    @JvmStatic external fun mpJoin(host: String, port: Int, name: String, faction: Int): Boolean
    @JvmStatic external fun mpShutdown()
    @JvmStatic external fun mpSetMap(mapPath: String): Boolean
    @JvmStatic external fun mpSetReady(ready: Boolean): Boolean
    @JvmStatic external fun mpStartMatch(): Boolean
    @JvmStatic external fun mpIsHost(): Boolean
    @JvmStatic external fun mpLocalSlot(): Int
    @JvmStatic external fun mpPlayerCount(): Int
    @JvmStatic external fun mpIsLockstep(): Boolean
    /** Lines: slot|name|faction|ready|connected */
    @JvmStatic external fun mpPlayersText(): String
    @JvmStatic external fun mpConsumeMatchStarted(): Boolean
}
