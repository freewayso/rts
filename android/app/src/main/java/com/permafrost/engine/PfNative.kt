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
}
