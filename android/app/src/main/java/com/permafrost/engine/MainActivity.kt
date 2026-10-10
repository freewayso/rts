package com.permafrost.engine

import android.content.res.AssetManager
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import java.io.FileOutputStream
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Thin Activity host: copies assets → filesDir, loads libpermafrost.so,
 * drives pf_android_frame on the GL thread, LAN lobby overlay on UI thread.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var glView: GLSurfaceView
    private lateinit var lobbyPanel: LinearLayout
    private lateinit var btnShowLobby: Button
    private lateinit var txtStatus: TextView
    private lateinit var txtPlayers: TextView
    private lateinit var btnStart: Button
    private lateinit var editName: EditText
    private lateinit var editHost: EditText
    private lateinit var editPort: EditText

    private var nativeReady = false
    private var hosting = false
    private val uiHandler = Handler(Looper.getMainLooper())
    private val lobbyPoll = object : Runnable {
        override fun run() {
            if (!nativeReady) {
                uiHandler.postDelayed(this, 500)
                return
            }
            glView.queueEvent {
                val players = PfNative.mpPlayersText()
                val count = PfNative.mpPlayerCount()
                val slot = PfNative.mpLocalSlot()
                val lockstep = PfNative.mpIsLockstep()
                val started = PfNative.mpConsumeMatchStarted()
                val isHost = PfNative.mpIsHost()
                uiHandler.post {
                    txtPlayers.text = if (players.isBlank()) "(no players)" else players.trim()
                    btnStart.isEnabled = hosting && isHost && !lockstep
                    when {
                        lockstep -> {
                            txtStatus.text = "Lockstep playing (slot=$slot)"
                            hideLobby()
                        }
                        hosting || count > 0 ->
                            txtStatus.text = "Lobby players=$count slot=$slot host=$isHost"
                    }
                    if (started) {
                        Toast.makeText(this@MainActivity, "Match started", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            uiHandler.postDelayed(this, 500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val files = filesDir.absolutePath
        AssetPacker.copyTree(
            assets, files,
            listOf("maps", "models", "shaders", "shaders_es", "tiles", "skyboxes", "icons")
        )

        setContentView(R.layout.activity_main)
        val root = findViewById<FrameLayout>(R.id.root)
        lobbyPanel = findViewById(R.id.lobbyPanel)
        btnShowLobby = findViewById(R.id.btnShowLobby)
        txtStatus = findViewById(R.id.txtStatus)
        txtPlayers = findViewById(R.id.txtPlayers)
        btnStart = findViewById(R.id.btnStart)
        editName = findViewById(R.id.editName)
        editHost = findViewById(R.id.editHost)
        editPort = findViewById(R.id.editPort)

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            setEGLConfigChooser(8, 8, 8, 8, 24, 8)
            setRenderer(PfRenderer(files))
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        root.addView(
            glView,
            0,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        findViewById<Button>(R.id.btnHost).setOnClickListener { onHostClicked() }
        findViewById<Button>(R.id.btnJoin).setOnClickListener { onJoinClicked() }
        btnStart.setOnClickListener { onStartClicked() }
        findViewById<Button>(R.id.btnHide).setOnClickListener { hideLobby() }
        btnShowLobby.setOnClickListener { showLobby() }

        uiHandler.post(lobbyPoll)
        Log.i(TAG, "MainActivity created; assets → $files")
    }

    private fun portOrDefault(): Int =
        editPort.text.toString().toIntOrNull() ?: 27750

    private fun onHostClicked() {
        if (!nativeReady) {
            toast("Native not ready")
            return
        }
        val name = editName.text.toString().ifBlank { "AndroidHost" }
        val port = portOrDefault()
        glView.queueEvent {
            val ok = PfNative.mpHost(port, name, 1)
            uiHandler.post {
                hosting = ok
                btnStart.isEnabled = ok
                txtStatus.text = if (ok) "Hosting on :$port" else "Host failed"
                toast(if (ok) "Hosting UDP $port" else "Host failed")
            }
        }
    }

    private fun onJoinClicked() {
        if (!nativeReady) {
            toast("Native not ready")
            return
        }
        val name = editName.text.toString().ifBlank { "AndroidClient" }
        val host = editHost.text.toString().ifBlank { "127.0.0.1" }
        val port = portOrDefault()
        glView.queueEvent {
            val ok = PfNative.mpJoin(host, port, name, 2)
            uiHandler.post {
                hosting = false
                btnStart.isEnabled = false
                txtStatus.text = if (ok) "Joined $host:$port" else "Join failed — retry"
                toast(if (ok) "Joined $host:$port" else "Join failed")
            }
        }
    }

    private fun onStartClicked() {
        if (!nativeReady) return
        glView.queueEvent {
            val ok = PfNative.mpStartMatch()
            uiHandler.post {
                txtStatus.text = if (ok) "Start sent…" else "Start failed (need peers?)"
                toast(if (ok) "Start match" else "Start failed")
            }
        }
    }

    private fun hideLobby() {
        lobbyPanel.visibility = View.GONE
        btnShowLobby.visibility = View.VISIBLE
    }

    private fun showLobby() {
        lobbyPanel.visibility = View.VISIBLE
        btnShowLobby.visibility = View.GONE
    }

    private fun toast(msg: String) =
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

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
        uiHandler.removeCallbacks(lobbyPoll)
        if (nativeReady) {
            glView.queueEvent { PfNative.mpShutdown() }
            PfNative.shutdown()
        }
        nativeReady = false
        super.onDestroy()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!nativeReady) return super.onTouchEvent(event)
        /* Let lobby controls receive clicks when visible and hit. */
        if (lobbyPanel.visibility == View.VISIBLE) {
            val loc = IntArray(2)
            lobbyPanel.getLocationOnScreen(loc)
            val x = event.rawX
            val y = event.rawY
            if (x >= loc[0] && x <= loc[0] + lobbyPanel.width &&
                y >= loc[1] && y <= loc[1] + lobbyPanel.height
            ) {
                return super.onTouchEvent(event)
            }
        }
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
            Log.d(TAG, "skip $assetPath: ${e.message}")
        }
    }
}
