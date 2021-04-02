#include <android/log.h>

#include "library/common/jni/jni_support.h"

// NOLINT(namespace-envoy)

int jni_log(const char* tag, const char* fmt, void** thr_args) {
  return __android_log_print(ANDROID_LOG_VERBOSE, tag, fmt, thr_args);
}

jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
  return vm->AttachCurrentThread(p_env, thr_args);
}
