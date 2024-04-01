#include <jni.h>

#include "test/test_common/utility.h"

#include "library/jni/jni_support.h"

// NOLINT(namespace-envoy)

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
