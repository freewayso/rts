package com.permafrost.engine

import android.content.res.AssetManager
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import android.view.MotionEvent
import android.view.WindowManager
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import java.io.FileOutputStream
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Thin Activity host: copies assets → filesDir, loads libpermafrost.so,
 * drives pf_android_frame on the GL thread (single-thread render).
 */
class MainActivity : AppCompatActivity() {

    private lateinit var glView: GLSurfaceView
    private var nativeReady = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val files = filesDir.absolutePath
        AssetPacker.copyTree(assets, files, listOf("maps", "models", "shaders", "shaders_es", "tiles"))

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)
            setRenderer(PfRenderer(files))
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        setContentView(glView)
        Log.i(TAG, "MainActivity created; assets → $files")
    }

    override fun onPause() {
        super.onPause()
        glView.onPause()
        if (nativeReady) PfNative.pause()
    }

    override fun onResume() {
        super.onResume()
        glView.onResume()
        if (nativeReady) PfNative.resume()
    }

    override fun onDestroy() {
        if (nativeReady) PfNative.shutdown()
        nativeReady = false
        super.onDestroy()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!nativeReady) return super.onTouchEvent(event)
        val id = event.getPointerId(event.actionIndex)
        val x = event.getX(event.actionIndex)
        val y = event.getY(event.actionIndex)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
                PfNative.touch(0, x, y, id)
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    PfNative.touch(1, event.getX(i), event.getY(i), event.getPointerId(i))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL ->
                PfNative.touch(2, x, y, id)
        }
        return true
    }

    private inner class PfRenderer(private val filesDir: String) : GLSurfaceView.Renderer {
        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            val ok = PfNative.init(filesDir, glView.width.coerceAtLeast(1), glView.height.coerceAtLeast(1))
            nativeReady = ok
            PfNative.setQuality(QualityPrefs.load(this@MainActivity))
            Log.i(TAG, "native init=$ok")
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            if (nativeReady) PfNative.resize(width, height)
        }

        override fun onDrawFrame(gl: GL10?) {
            if (nativeReady) PfNative.frame()
        }
    }

    companion object {
        private const val TAG = "PfActivity"
    }
}

/** Copy selected asset folders into writable filesDir for g_basepath. */
object AssetPacker {
    private const val TAG = "PfAssets"

    fun copyTree(am: AssetManager, destRoot: String, folders: List<String>) {
        for (folder in folders) {
            copyDir(am, folder, File(destRoot, folder))
        }
        // Also copy any top-level demo map markers
        try {
            val names = am.list("") ?: return
            for (n in names) {
                if (n.endsWith(".pfmap") || n == "README.txt") {
                    copyFile(am, n, File(destRoot, n))
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "top-level list: ${e.message}")
        }
    }

    private fun copyDir(am: AssetManager, assetPath: String, dest: File) {
        val children = try {
            am.list(assetPath)
        } catch (e: Exception) {
            null
        } ?: return
        if (children.isEmpty()) {
            // May be a file
            copyFile(am, assetPath, dest)
            return
        }
        if (!dest.exists()) dest.mkdirs()
        for (child in children) {
            val childAsset = if (assetPath.isEmpty()) child else "$assetPath/$child"
            copyDir(am, childAsset, File(dest, child))
        }
    }

    private fun copyFile(am: AssetManager, assetPath: String, dest: File) {
        try {
            if (dest.exists() && dest.isFile && dest.length() > 0) return
            dest.parentFile?.mkdirs()
            am.open(assetPath).use { input ->
                FileOutputStream(dest).use { output -> input.copyTo(output) }
            }
            Log.d(TAG, "copied $assetPath")
        } catch (e: Exception) {
            // Missing optional folders are fine
            Log.d(TAG, "skip $assetPath: ${e.message}")
        }
    }
}
