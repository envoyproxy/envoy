#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_utility.h"

// NOLINT(namespace-envoy)

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject class_loader) {
  Envoy::JNI::setClassLoader(env->NewGlobalRef(class_loader));
  return ENVOY_SUCCESS;
}
