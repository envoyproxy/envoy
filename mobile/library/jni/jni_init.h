#pragma once

#include <jni.h>

namespace Envoy {
namespace JNI {

/** Initializes the JNI code for Envoy Mobile. This is typically called in `JNI_OnLoad`. */
void initialize(JavaVM* jvm);

/** Performs a cleanup in the JNI code. This is typically called in `JNI_OnUnload`. */
void finalize();

} // namespace JNI
} // namespace Envoy
