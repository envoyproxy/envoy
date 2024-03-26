#pragma once

#include "library/jni/import/jni_import.h"

// NOLINT(namespace-envoy)

extern "C" jint attach_jvm(JavaVM* vm, JNIEnv** p_env, void* thr_args);
