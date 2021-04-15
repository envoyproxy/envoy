#include <jni.h>

#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/main_interface.h"

// NOLINT(namespace-envoy)

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject connectivity_manager) {

  return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_setPreferredNetwork(JNIEnv* env,
                                                                            jclass, // class
                                                                            jint network) {
  jni_log("[Envoy]", "setting preferred network");
  return set_preferred_network(static_cast<envoy_network_t>(network));
}
