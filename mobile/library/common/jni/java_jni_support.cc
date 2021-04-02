#include "library/common/jni/jni_support.h"

// NOLINT(namespace-envoy)

int jni_log(const char* tag, const char* fmt, ...) { return 0; }

jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
  return vm->AttachCurrentThread(reinterpret_cast<void**>(p_env), thr_args);
}
