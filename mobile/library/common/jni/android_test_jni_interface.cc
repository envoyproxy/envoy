#include "library/common/jni/android_network_utility.h"
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
  // At this point, we know Android APIs are available. Register cert chain validation JNI calls.
  return register_platform_api(cert_validator_name, get_android_cert_validator_api());
}
