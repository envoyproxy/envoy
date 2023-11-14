#include <jni.h>

#include "library/common/jni/jni_utility.h"

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
