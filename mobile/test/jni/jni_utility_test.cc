#include <jni.h>

#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "library/common/http/header_utility.h"
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

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppStringConversion(JNIEnv* env, jclass,
                                                                          jstring java_string) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_string = Envoy::JNI::javaStringToCppString(jni_helper, java_string);
  return Envoy::JNI::cppStringToJavaString(jni_helper, cpp_string).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppMapConversion(JNIEnv* env, jclass,
                                                                       jobject java_map) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_map = Envoy::JNI::javaMapToCppMap(jni_helper, java_map);
  return Envoy::JNI::cppMapToJavaMap(jni_helper, cpp_map).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppHeadersConversion(JNIEnv* env, jclass,
                                                                           jobject java_headers) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_headers = Envoy::Http::Utility::createRequestHeaderMapPtr();
  Envoy::JNI::javaHeadersToCppHeaders(jni_helper, java_headers, *cpp_headers);
  return Envoy::JNI::cppHeadersToJavaHeaders(jni_helper, *cpp_headers).release();
}
