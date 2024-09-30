#include <jni.h>

#include "test/common/integration/test_server.h"

#include "extension_registry.h"
#include "library/jni/jni_helper.h"
#include "library/jni/jni_utility.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
  Envoy::JNI::JniHelper::initialize(vm);
  Envoy::JNI::JniHelper::addToCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpProxyTestServerFactory$HttpProxyTestServer",
      /* methods= */
      {
          {"<init>", "(JLjava/lang/String;I)V"},
      },
      /* static_methods= */ {}, /* fields= */
      {
          {"handle", "J"},
      },
      /* static_fields= */ {});
  return Envoy::JNI::JniHelper::getVersion();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpProxyTestServerFactory_start(JNIEnv* env, jclass,
                                                                               jint type) {
  Envoy::JNI::JniHelper jni_helper(env);

  Envoy::ExtensionRegistry::registerFactories();
  Envoy::TestServer* test_server = new Envoy::TestServer();
  test_server->start(static_cast<Envoy::TestServerType>(type), 0);

  jclass java_http_proxy_server_factory_class = jni_helper.findClassFromCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpProxyTestServerFactory$HttpProxyTestServer");
  auto java_init_method_id = jni_helper.getMethodIdFromCache(java_http_proxy_server_factory_class,
                                                             "<init>", "(JLjava/lang/String;I)V");
  auto ip_address = Envoy::JNI::cppStringToJavaString(jni_helper, test_server->getIpAddress());
  int port = test_server->getPort();
  return jni_helper
      .newObject(java_http_proxy_server_factory_class, java_init_method_id,
                 reinterpret_cast<jlong>(test_server), ip_address.get(), static_cast<jint>(port))
      .release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpProxyTestServerFactory_00024HttpProxyTestServer_shutdown(
    JNIEnv* env, jobject instance) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.findClassFromCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpProxyTestServerFactory$HttpProxyTestServer");
  auto java_handle_field_id = jni_helper.getFieldIdFromCache(java_class, "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::TestServer* test_server = reinterpret_cast<Envoy::TestServer*>(java_handle);
  test_server->shutdown();
  delete test_server;
}
