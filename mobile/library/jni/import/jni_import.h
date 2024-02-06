#pragma once

#include <jni.h>

// NOLINT(namespace-envoy)

// This validates that the jni.h header that we include is *not* the jni.h provided by the JVM. This
// helps ensure that the build is using a consistent jni.h header.
#if defined(__ANDROID_API__) && defined(_JAVASOFT_JNI_H_)
#error "JVM jni.h imported during android build"
#endif
