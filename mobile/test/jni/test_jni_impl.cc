#include <jni.h>

#include "test/test_common/utility.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeCreateProtoString(JNIEnv* env, jclass,
                                                                              jlong bootstrap_ptr) {
  Envoy::Thread::SkipAsserts skip_asserts;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
  std::string proto_str = bootstrap->ShortDebugString();
  return env->NewStringUTF(proto_str.c_str());
}
