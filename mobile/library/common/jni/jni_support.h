#include <jni.h>

// NOLINT(namespace-envoy)

int jni_log(const char* tag, const char* fmt, ...);

jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args);
