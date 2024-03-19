#include "library/jni/jni_support.h"

// NOLINT(namespace-envoy)

jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
  return vm->AttachCurrentThread(p_env, thr_args);
}
