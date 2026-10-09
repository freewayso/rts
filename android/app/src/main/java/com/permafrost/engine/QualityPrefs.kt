package com.permafrost.engine

import android.content.Context
import android.util.Log

/**
 * Persist quality preset (0=low 1=medium 2=high) across process restarts.
 * Low: water FX off, intended for thermal/perf headroom.
 */
object QualityPrefs {
    private const val PREFS = "pf_video"
    private const val KEY = "quality"
    private const val TAG = "PfQuality"

    fun load(ctx: Context): Int {
        val q = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt(KEY, 1)
        Log.i(TAG, "loaded quality=$q")
        return q.coerceIn(0, 2)
    }

    fun save(ctx: Context, level: Int) {
        val q = level.coerceIn(0, 2)
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putInt(KEY, q)
            .apply()
        PfNative.setQuality(q)
        Log.i(TAG, "saved quality=$q")
    }
}
