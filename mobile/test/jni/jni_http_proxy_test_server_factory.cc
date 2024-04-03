#include <jni.h>

#include "test/common/integration/test_server.h"

#include "extension_registry.h"
#include "library/jni/jni_helper.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpProxyTestServerFactory_start(JNIEnv* env, jclass,
                                                                               jint type) {
  Envoy::JNI::JniHelper jni_helper(env);

  Envoy::ExtensionRegistry::registerFactories();
  Envoy::TestServer* test_server = new Envoy::TestServer();
  test_server->startTestServer(static_cast<Envoy::TestServerType>(type));

  auto java_http_proxy_server_factory_class = jni_helper.findClass(
      "io/envoyproxy/envoymobile/engine/testing/HttpProxyTestServerFactory$HttpProxyTestServer");
  auto java_init_method_id =
      jni_helper.getMethodId(java_http_proxy_server_factory_class.get(), "<init>", "(JI)V");
  int port = test_server->getServerPort();
  return jni_helper
      .newObject(java_http_proxy_server_factory_class.get(), java_init_method_id,
                 reinterpret_cast<jlong>(test_server), static_cast<jint>(port))
      .release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpProxyTestServerFactory_00024HttpProxyTestServer_shutdown(
    JNIEnv* env, jobject instance) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.getObjectClass(instance);
  auto java_handle_field_id = jni_helper.getFieldId(java_class.get(), "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::TestServer* test_server = reinterpret_cast<Envoy::TestServer*>(java_handle);
  test_server->shutdownTestServer();
  delete test_server;
}
