/*
 * JNI bridge for Kotlin/Java Activity.
 */
#include "pf_android.h"

#include <jni.h>
#include <android/log.h>

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
