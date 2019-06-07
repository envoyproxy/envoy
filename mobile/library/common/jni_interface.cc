#include <ares.h>
#include <jni.h>
#include <string.h>

#include "main_interface.h"

// NOLINT(namespace-envoy)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env = nullptr;

  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    return -1;
  }

  ares_library_init_jvm(vm);
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_Envoy_runEnvoy(JNIEnv* env,
                                                                                jobject, // this
                                                                                jstring config) {
  return run_envoy(env->GetStringUTFChars(config, nullptr));
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_Envoy_initialize(JNIEnv* env,
                                                jobject, // this
                                                jobject connectivity_manager) {
  return ares_library_init_android(connectivity_manager);
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_envoyproxy_envoymobile_Envoy_isAresInitialized(
    JNIEnv* env,
    jobject // this
) {
  return ares_library_android_initialized() == ARES_SUCCESS;
}
