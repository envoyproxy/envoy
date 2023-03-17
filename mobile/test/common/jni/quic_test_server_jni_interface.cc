#include <jni.h>

#include "test/common/integration/quic_test_server_interface.h"

#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/jni/jni_version.h"

// NOLINT(namespace-envoy)

// Quic Test ServerJniLibrary

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_QuicTestServer_nativeStartQuicTestServer(
    JNIEnv* env, jclass clazz) {
  jni_log("[QTS]", "starting server");
  start_server();
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_QuicTestServer_nativeGetServerPort(JNIEnv* env,
                                                                                 jclass clazz) {
  jni_log("[QTS]", "getting server port");
  return get_server_port();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_QuicTestServer_nativeShutdownQuicTestServer(
    JNIEnv* env, jclass clazz) {
  jni_log("[QTS]", "shutting down server");
  shutdown_server();
}
