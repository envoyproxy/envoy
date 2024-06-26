#include <jni.h>

#include "test/common/integration/xds_test_server.h"

#include "extension_registry.h"
#include "library/jni/jni_helper.h"
#include "library/jni/jni_utility.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
  Envoy::JNI::JniHelper::initialize(vm);
  Envoy::JNI::JniHelper::addClassToCache("java/util/Map$Entry");
  Envoy::JNI::JniHelper::addClassToCache(
      "io/envoyproxy/envoymobile/engine/testing/XdsTestServerFactory$XdsTestServer");
  return Envoy::JNI::JniHelper::getVersion();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_XdsTestServerFactory_create(JNIEnv* env, jclass) {
  // This is called via JNI from kotlin tests, and Envoy doesn't consider it a test thread
  // which triggers some failures of `ASSERT_IS_MAIN_OR_TEST_THREAD()`.
  Envoy::Thread::SkipAsserts skip;
  Envoy::JNI::JniHelper jni_helper(env);

  Envoy::ExtensionRegistry::registerFactories();
  Envoy::XdsTestServer* test_server = new Envoy::XdsTestServer();

  jclass java_xds_server_factory_class = jni_helper.findClass(
      "io/envoyproxy/envoymobile/engine/testing/XdsTestServerFactory$XdsTestServer");
  auto java_init_method_id =
      jni_helper.getMethodId(java_xds_server_factory_class, "<init>", "(JLjava/lang/String;I)V");
  auto host = Envoy::JNI::cppStringToJavaString(jni_helper, test_server->getHost());
  jint port = static_cast<jint>(test_server->getPort());
  return jni_helper
      .newObject(java_xds_server_factory_class, java_init_method_id,
                 reinterpret_cast<jlong>(test_server), host.get(), port)
      .release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_XdsTestServerFactory_00024XdsTestServer_start(
    JNIEnv* env, jobject instance) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.getObjectClass(instance);
  auto java_handle_field_id = jni_helper.getFieldId(java_class.get(), "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::XdsTestServer* test_server = reinterpret_cast<Envoy::XdsTestServer*>(java_handle);
  test_server->start();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_XdsTestServerFactory_00024XdsTestServer_sendDiscoveryResponse(
    JNIEnv* env, jobject instance, jstring cluster_name) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.getObjectClass(instance);
  auto java_handle_field_id = jni_helper.getFieldId(java_class.get(), "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::XdsTestServer* test_server = reinterpret_cast<Envoy::XdsTestServer*>(java_handle);

  envoy::service::discovery::v3::DiscoveryResponse response;
  *response.mutable_version_info() = "v1";
  envoy::config::cluster::v3::Cluster cluster;
  *cluster.mutable_name() = Envoy::JNI::javaStringToCppString(jni_helper, cluster_name);
  response.add_resources()->PackFrom(cluster);
  *response.mutable_type_url() = "type.googleapis.com/envoy.config.cluster.v3.Cluster";

  test_server->send(response);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_XdsTestServerFactory_00024XdsTestServer_shutdown(
    JNIEnv* env, jobject instance) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.getObjectClass(instance);
  auto java_handle_field_id = jni_helper.getFieldId(java_class.get(), "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::XdsTestServer* test_server = reinterpret_cast<Envoy::XdsTestServer*>(java_handle);
  test_server->shutdown();
  delete test_server;
}
