#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/main_interface.h"

// NOLINT(namespace-envoy)

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject class_loader,
                                                                   jobject connectivity_manager) {
  set_class_loader(env->NewGlobalRef(class_loader));

  return 0;
}
