#include <jni.h>

#include "test/common/integration/test_server.h"

#include "extension_registry.h"
#include "library/jni/jni_helper.h"
#include "library/jni/jni_utility.h"

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
  Envoy::JNI::JniHelper::initialize(vm);
  Envoy::JNI::JniHelper::addToCache("java/util/Map", /* methods= */
                                    {
                                        {"entrySet", "()Ljava/util/Set;"},

                                    },
                                    /* static_methods = */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Map$Entry", /* methods= */
                                    {
                                        {"getKey", "()Ljava/lang/Object;"},
                                        {"getValue", "()Ljava/lang/Object;"},

                                    },
                                    /* static_methods = */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Set", /* methods= */
                                    {
                                        {"iterator", "()Ljava/util/Iterator;"},

                                    },
                                    /* static_methods = */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Iterator", /* methods= */
                                    {
                                        {"hasNext", "()Z"},
                                        {"next", "()Ljava/lang/Object;"},

                                    },
                                    /* static_methods = */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpTestServerFactory$HttpTestServer",
      /* methods= */
      {
          {"<init>", "(JLjava/lang/String;ILjava/lang/String;)V"},
      },
      /* static_methods= */ {}, /* fields= */
      {
          {"handle", "J"},
      },
      /* static_fields= */ {});
  return Envoy::JNI::JniHelper::getVersion();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpTestServerFactory_start(
    JNIEnv* env, jclass, jint type, jint requested_port, jobject headers, jstring body,
    jobject trailers) {
  Envoy::JNI::JniHelper jni_helper(env);

  Envoy::ExtensionRegistry::registerFactories();
  Envoy::TestServer* test_server = new Envoy::TestServer();
  test_server->start(static_cast<Envoy::TestServerType>(type), requested_port);

  auto cpp_headers = Envoy::JNI::javaMapToCppMap(jni_helper, headers);
  auto cpp_body = Envoy::JNI::javaStringToCppString(jni_helper, body);
  auto cpp_trailers = Envoy::JNI::javaMapToCppMap(jni_helper, trailers);
  test_server->setResponse(cpp_headers, cpp_body, cpp_trailers);

  jclass java_http_server_factory_class = jni_helper.findClassFromCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpTestServerFactory$HttpTestServer");
  auto java_init_method_id = jni_helper.getMethodIdFromCache(
      java_http_server_factory_class, "<init>", "(JLjava/lang/String;ILjava/lang/String;)V");
  auto ip_address = Envoy::JNI::cppStringToJavaString(jni_helper, test_server->getIpAddress());
  int port = test_server->getPort();
  auto address = Envoy::JNI::cppStringToJavaString(jni_helper, test_server->getAddress());
  return jni_helper
      .newObject(java_http_server_factory_class, java_init_method_id,
                 reinterpret_cast<jlong>(test_server), ip_address.get(), static_cast<jint>(port),
                 address.get())
      .release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_testing_HttpTestServerFactory_00024HttpTestServer_shutdown(
    JNIEnv* env, jobject instance) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_class = jni_helper.findClassFromCache(
      "io/envoyproxy/envoymobile/engine/testing/HttpTestServerFactory$HttpTestServer");
  auto java_handle_field_id = jni_helper.getFieldIdFromCache(java_class, "handle", "J");
  jlong java_handle = jni_helper.getLongField(instance, java_handle_field_id);
  Envoy::TestServer* test_server = reinterpret_cast<Envoy::TestServer*>(java_handle);
  test_server->shutdown();
  delete test_server;
}
