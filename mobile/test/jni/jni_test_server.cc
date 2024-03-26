#include <jni.h>

#include "test/common/integration/test_server_interface.h"
#include "test/common/integration/xds_test_server_interface.h"
#include "test/test_common/utility.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeStartHttpProxyTestServer(JNIEnv*,
                                                                                        jclass) {
  start_server(Envoy::TestServerType::HTTP_PROXY);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeStartHttpsProxyTestServer(
    JNIEnv* env, jclass clazz) {
  start_server(Envoy::TestServerType::HTTPS_PROXY);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeStartHttp3TestServer(JNIEnv*,
                                                                                    jclass) {
  start_server(Envoy::TestServerType::HTTP3);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeGetServerPort(JNIEnv*, jclass) {
  return get_server_port();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeStartHttp2TestServer(JNIEnv*,
                                                                                    jclass) {
  start_server(Envoy::TestServerType::HTTP2_WITH_TLS);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeShutdownTestServer(JNIEnv*, jclass) {
  shutdown_server();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeInitXdsTestServer(JNIEnv*, jclass) {
  initXdsServer();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeStartXdsTestServer(JNIEnv*, jclass) {
  startXdsServer();
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeGetXdsTestServerHost(JNIEnv* env,
                                                                                    jclass) {
  return env->NewStringUTF(getXdsServerHost());
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeGetXdsTestServerPort(JNIEnv*,
                                                                                    jclass) {
  return getXdsServerPort();
}

#ifdef ENVOY_ENABLE_YAML
extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeSendDiscoveryResponse(JNIEnv* env,
                                                                                     jclass,
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
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeShutdownXdsTestServer(JNIEnv*,
                                                                                     jclass) {
  shutdownXdsServer();
}

#ifdef ENVOY_ENABLE_YAML
extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_TestServer_nativeCreateYaml(JNIEnv* env, jclass,
                                                                          jlong bootstrap_ptr) {
  Envoy::Thread::SkipAsserts skip_asserts;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
  std::string yaml = Envoy::MessageUtil::getYamlStringFromMessage(*bootstrap);
  return env->NewStringUTF(yaml.c_str());
}
#endif
