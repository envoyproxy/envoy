#include <jni.h>

#include "absl/container/flat_hash_map.h"
#include "library/jni/jni_utility.h"

// NOLINT(namespace-envoy)

// This file contains JNI implementation used by
// `test/java/io/envoyproxy/envoymobile/jni/JniUtilityTest.java` unit tests.

extern "C" JNIEXPORT jbyteArray JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_protoJavaByteArrayConversion(JNIEnv* env, jclass,
                                                                               jbyteArray source) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::ProtobufWkt::Struct s;
  Envoy::JNI::javaByteArrayToProto(jni_helper, source, &s);
  return Envoy::JNI::protoToJavaByteArray(jni_helper, s).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_cppMapToJavaMap(JNIEnv* env, jclass) {
  Envoy::JNI::JniHelper jni_helper(env);
  absl::flat_hash_map<std::string, std::string> cpp_map{
      {"key1", "value1"},
      {"key2", "value2"},
      {"key3", "value3"},
  };
  return Envoy::JNI::cppMapToJavaMap(jni_helper, cpp_map).release();
}
