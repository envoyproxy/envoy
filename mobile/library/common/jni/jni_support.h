#include <jni.h>

// NOLINT(namespace-envoy)

extern "C" int jni_log_fmt(const char* tag, const char* fmt, void* value);

extern "C" int jni_log(const char* tag, const char* str);

extern "C" jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args);
