#include <ares.h>

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
  envoy_status_t result =
      register_platform_api(cert_validator_name, get_android_cert_validator_api());
  if (result == ENVOY_FAILURE) {
    return ENVOY_FAILURE;
  }

  // See note above about c-ares.
  // c-ares jvm init is necessary in order to let c-ares perform DNS resolution in Envoy.
  // More information can be found at:
  // https://c-ares.haxx.se/ares_library_init_android.html
  ares_library_init_jvm(get_vm());

  return ares_library_init_android(connectivity_manager);
}
