#include <jni.h>

#include "test/common/integration/xds_test_server_interface.h"
#include "test/test_common/utility.h"

#include "library/jni/jni_support.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeInitXdsTestServer(JNIEnv* env,
                                                                              jclass clazz) {
  initXdsServer();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeStartXdsTestServer(JNIEnv* env,
                                                                               jclass clazz) {
  startXdsServer();
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeGetXdsTestServerHost(JNIEnv* env,
                                                                                 jclass clazz) {
  return env->NewStringUTF(getXdsServerHost());
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeGetXdsTestServerPort(JNIEnv* env,
                                                                                 jclass clazz) {
  return getXdsServerPort();
}

#ifdef ENVOY_ENABLE_YAML
extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeSendDiscoveryResponse(JNIEnv* env,
                                                                                  jclass clazz,
                                                                                  jstring yaml) {
  const char* yaml_chars = env->GetStringUTFChars(yaml, /* isCopy= */ nullptr);
  // The yaml utilities have non-relevant thread asserts.
  Envoy::Thread::SkipAsserts skip;
  envoy::service::discovery::v3::DiscoveryResponse response;
  Envoy::TestUtility::loadFromYaml(yaml_chars, response);
  sendDiscoveryResponse(response);
  env->ReleaseStringUTFChars(yaml, yaml_chars);
}
#endif

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeShutdownXdsTestServer(JNIEnv* env,
                                                                                  jclass clazz) {
  shutdownXdsServer();
}

#ifdef ENVOY_ENABLE_YAML
extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestJni_nativeCreateYaml(JNIEnv* env, jclass,
                                                                       jlong bootstrap_ptr) {
  Envoy::Thread::SkipAsserts skip_asserts;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
  std::string yaml = Envoy::MessageUtil::getYamlStringFromMessage(*bootstrap);
  return env->NewStringUTF(yaml.c_str());
}
#endif
