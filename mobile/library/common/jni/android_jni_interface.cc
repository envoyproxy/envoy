#include <android/log.h>
#include <ares.h>
#include <jni.h>

#include "library/common/jni/jni_utility.h"
#include "library/common/main_interface.h"

// NOLINT(namespace-envoy)

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject connectivity_manager) {
  // See note above about c-ares.
  // c-ares jvm init is necessary in order to let c-ares perform DNS resolution in Envoy.
  // More information can be found at:
  // https://c-ares.haxx.se/ares_library_init_android.html
  ares_library_init_jvm(get_vm());

  return ares_library_init_android(connectivity_manager);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_setPreferredNetwork(JNIEnv* env,
                                                                            jclass, // class
                                                                            jint network) {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "setting preferred network");
  return set_preferred_network(static_cast<envoy_network_t>(network));
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_flushStats(JNIEnv* env,
                                                                   jclass // class
) {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "triggering stats flush");
  flush_stats();
}
