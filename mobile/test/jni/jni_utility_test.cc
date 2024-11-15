#include <jni.h>

#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "library/common/http/header_utility.h"
#include "library/jni/jni_utility.h"

// NOLINT(namespace-envoy)

// This file contains JNI implementation used by
// `test/java/io/envoyproxy/envoymobile/jni/JniUtilityTest.java` unit tests.

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
  Envoy::JNI::JniHelper::initialize(vm);
  Envoy::JNI::JniUtility::initCache();
  return Envoy::JNI::JniHelper::getVersion();
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void* /* reserved */) {
  Envoy::JNI::JniHelper::finalize();
}

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

extern "C" JNIEXPORT jboolean JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_isJavaDirectByteBuffer(JNIEnv* env, jclass,
                                                                         jobject java_byte_buffer) {
  Envoy::JNI::JniHelper jni_helper(env);
  return Envoy::JNI::isJavaDirectByteBuffer(jni_helper, java_byte_buffer);
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppDirectByteBufferConversion(
    JNIEnv* env, jclass, jobject java_byte_buffer, jlong length) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_buffer_instance =
      Envoy::JNI::javaDirectByteBufferToCppBufferInstance(jni_helper, java_byte_buffer, length);
  return Envoy::JNI::cppBufferInstanceToJavaDirectByteBuffer(jni_helper, *cpp_buffer_instance,
                                                             length)
      .release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppNonDirectByteBufferConversion(
    JNIEnv* env, jclass, jobject java_byte_buffer, jlong length) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_buffer_instance =
      Envoy::JNI::javaNonDirectByteBufferToCppBufferInstance(jni_helper, java_byte_buffer, length);
  return Envoy::JNI::cppBufferInstanceToJavaNonDirectByteBuffer(jni_helper, *cpp_buffer_instance,
                                                                length)
      .release();
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_getJavaExceptionMessage(JNIEnv* env, jclass,
                                                                          jthrowable throwble) {
  Envoy::JNI::JniHelper jni_helper(env);
  std::string exception_message = Envoy::JNI::getJavaExceptionMessage(jni_helper, throwble);
  return Envoy::JNI::cppStringToJavaString(jni_helper, exception_message).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppStreamIntelConversion(
    JNIEnv* env, jclass, jobject java_stream_intel) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_stream_intel =
      Envoy::JNI::javaStreamIntelToCppStreamIntel(jni_helper, java_stream_intel);
  return Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, cpp_stream_intel).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniUtilityTest_javaCppFinalStreamIntelConversion(
    JNIEnv* env, jclass, jobject java_final_stream_intel) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_final_stream_intel =
      Envoy::JNI::javaFinalStreamIntelToCppFinalStreamIntel(jni_helper, java_final_stream_intel);
  return Envoy::JNI::cppFinalStreamIntelToJavaFinalStreamIntel(jni_helper, cpp_final_stream_intel)
      .release();
}
