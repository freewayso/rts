/*
 * JNI bridge for Kotlin/Java Activity.
 */
#include "pf_android.h"
#include "android_mp.h"

#include <jni.h>
#include <android/log.h>
#include <string.h>

#define LOG_TAG "PermafrostJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_init(JNIEnv *env, jclass clazz,
                                         jstring filesDir, jint w, jint h)
{
    (void)clazz;
    const char *dir = (*env)->GetStringUTFChars(env, filesDir, NULL);
    bool ok = pf_android_init(dir, (int)w, (int)h);
    (*env)->ReleaseStringUTFChars(env, filesDir, dir);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_resize(JNIEnv *env, jclass clazz, jint w, jint h)
{
    (void)env; (void)clazz;
    pf_android_resize((int)w, (int)h);
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_frame(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    pf_android_frame();
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_pause(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    pf_android_pause();
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_resume(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    pf_android_resume();
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_shutdown(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    pf_android_shutdown();
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_touch(JNIEnv *env, jclass clazz,
                                          jint type, jfloat x, jfloat y, jint pointerId)
{
    (void)env; (void)clazz;
    pf_android_touch((int)type, (float)x, (float)y, (int)pointerId);
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_setQuality(JNIEnv *env, jclass clazz, jint level)
{
    (void)env; (void)clazz;
    pf_android_set_quality((int)level);
}

JNIEXPORT jint JNICALL
Java_com_permafrost_engine_PfNative_getQuality(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return (jint)pf_android_get_quality();
}

/* ---- Multiplayer (must be invoked on GL thread via queueEvent) ---- */

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpHost(JNIEnv *env, jclass clazz,
                                           jint port, jstring name, jint faction)
{
    (void)clazz;
    const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    bool ok = pf_android_mp_host((uint16_t)port, n, (int)faction);
    if(n) (*env)->ReleaseStringUTFChars(env, name, n);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpJoin(JNIEnv *env, jclass clazz,
                                           jstring host, jint port, jstring name, jint faction)
{
    (void)clazz;
    const char *h = host ? (*env)->GetStringUTFChars(env, host, NULL) : NULL;
    const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    bool ok = pf_android_mp_join(h, (uint16_t)port, n, (int)faction);
    if(h) (*env)->ReleaseStringUTFChars(env, host, h);
    if(n) (*env)->ReleaseStringUTFChars(env, name, n);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_permafrost_engine_PfNative_mpShutdown(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    pf_android_mp_shutdown();
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpSetMap(JNIEnv *env, jclass clazz, jstring mapPath)
{
    (void)clazz;
    const char *p = mapPath ? (*env)->GetStringUTFChars(env, mapPath, NULL) : NULL;
    bool ok = pf_android_mp_set_map(p);
    if(p) (*env)->ReleaseStringUTFChars(env, mapPath, p);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpSetReady(JNIEnv *env, jclass clazz, jboolean ready)
{
    (void)env; (void)clazz;
    return pf_android_mp_set_ready(ready == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpStartMatch(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return pf_android_mp_start_match() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpIsHost(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return pf_android_mp_is_host() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_permafrost_engine_PfNative_mpLocalSlot(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return (jint)pf_android_mp_local_slot();
}

JNIEXPORT jint JNICALL
Java_com_permafrost_engine_PfNative_mpPlayerCount(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return (jint)pf_android_mp_player_count();
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpIsLockstep(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return pf_android_mp_is_lockstep() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_permafrost_engine_PfNative_mpPlayersText(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    char buf[1024];
    pf_android_mp_players_text(buf, sizeof(buf));
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jboolean JNICALL
Java_com_permafrost_engine_PfNative_mpConsumeMatchStarted(JNIEnv *env, jclass clazz)
{
    (void)env; (void)clazz;
    return pf_android_mp_consume_match_started() ? JNI_TRUE : JNI_FALSE;
}
