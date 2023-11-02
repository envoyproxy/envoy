#include <jni.h>

#include "library/common/jni/jni_helper.h"

// NOLINT(namespace-envoy)

// This file contains JNI implementation used by
// `test/java/io/envoyproxy/envoymobile/jni/JniHelperTest.java` unit tests.

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_getMethodId(
    JNIEnv* env, jclass, jclass clazz, jstring name, jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jni_helper.getMethodId(clazz, name_ptr.get(), sig_ptr.get());
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_getStaticMethodId(JNIEnv* env, jclass,
                                                                   jclass clazz, jstring name,
                                                                   jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jni_helper.getStaticMethodId(clazz, name_ptr.get(), sig_ptr.get());
}

extern "C" JNIEXPORT jclass JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_findClass(
    JNIEnv* env, jclass, jstring class_name) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr class_name_ptr = jni_helper.getStringUtfChars(class_name, nullptr);
  Envoy::JNI::LocalRefUniquePtr<jclass> clazz = jni_helper.findClass(class_name_ptr.get());
  return clazz.release();
}

extern "C" JNIEXPORT jclass JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_getObjectClass(
    JNIEnv* env, jclass, jobject object) {
  Envoy::JNI::JniHelper jni_helper(env);
  return jni_helper.getObjectClass(object).release();
}

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_throwNew(
    JNIEnv* env, jclass, jstring class_name, jstring message) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr class_name_ptr = jni_helper.getStringUtfChars(class_name, nullptr);
  Envoy::JNI::StringUtfUniquePtr message_ptr = jni_helper.getStringUtfChars(message, nullptr);
  jni_helper.throwNew(class_name_ptr.get(), message_ptr.get());
}

extern "C" JNIEXPORT jobject JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_newObject(
    JNIEnv* env, jclass, jclass clazz, jstring name, jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jmethodID method_id = jni_helper.getMethodId(clazz, name_ptr.get(), sig_ptr.get());
  return jni_helper.newObject(clazz, method_id).release();
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_getArrayLength(JNIEnv* env, jclass, jarray array) {
  Envoy::JNI::JniHelper jni_helper(env);
  return jni_helper.getArrayLength(array);
}

#define DEFINE_JNI_NEW_ARRAY(JAVA_TYPE, JNI_TYPE)                                                  \
  extern "C" JNIEXPORT JNI_TYPE JNICALL                                                            \
      Java_io_envoyproxy_envoymobile_jni_JniHelperTest_new##JAVA_TYPE##Array(JNIEnv* env, jclass,  \
                                                                             jsize length) {       \
    Envoy::JNI::JniHelper jni_helper(env);                                                         \
    return jni_helper.new##JAVA_TYPE##Array(length).release();                                     \
  }

DEFINE_JNI_NEW_ARRAY(Byte, jbyteArray)
DEFINE_JNI_NEW_ARRAY(Char, jcharArray)
DEFINE_JNI_NEW_ARRAY(Short, jshortArray)
DEFINE_JNI_NEW_ARRAY(Int, jintArray)
DEFINE_JNI_NEW_ARRAY(Long, jlongArray)
DEFINE_JNI_NEW_ARRAY(Float, jfloatArray)
DEFINE_JNI_NEW_ARRAY(Double, jdoubleArray)
DEFINE_JNI_NEW_ARRAY(Boolean, jbooleanArray)

extern "C" JNIEXPORT jobjectArray JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_newObjectArray(JNIEnv* env, jclass, jsize length,
                                                                jclass element_class,
                                                                jobject initial_element) {
  Envoy::JNI::JniHelper jni_helper(env);
  return jni_helper.newObjectArray(length, element_class, initial_element).release();
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_getObjectArrayElement(JNIEnv* env, jclass,
                                                                       jobjectArray array,
                                                                       jsize index) {
  Envoy::JNI::JniHelper jni_helper(env);
  return jni_helper.getObjectArrayElement(array, index).release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_setObjectArrayElement(JNIEnv* env, jclass,
                                                                       jobjectArray array,
                                                                       jsize index, jobject value) {
  Envoy::JNI::JniHelper jni_helper(env);
  jni_helper.setObjectArrayElement(array, index, value);
}

#define DEFINE_JNI_CALL_METHOD(JAVA_TYPE, JNI_TYPE)                                                \
  extern "C" JNIEXPORT JNI_TYPE JNICALL                                                            \
      Java_io_envoyproxy_envoymobile_jni_JniHelperTest_call##JAVA_TYPE##Method(                    \
          JNIEnv* env, jclass, jclass clazz, jobject object, jstring name, jstring signature) {    \
    Envoy::JNI::JniHelper jni_helper(env);                                                         \
    Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);         \
    Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);     \
    jmethodID method_id = jni_helper.getMethodId(clazz, name_ptr.get(), sig_ptr.get());            \
    return jni_helper.call##JAVA_TYPE##Method(object, method_id);                                  \
  }

DEFINE_JNI_CALL_METHOD(Byte, jbyte)
DEFINE_JNI_CALL_METHOD(Char, jchar)
DEFINE_JNI_CALL_METHOD(Short, jshort)
DEFINE_JNI_CALL_METHOD(Int, jint)
DEFINE_JNI_CALL_METHOD(Long, jlong)
DEFINE_JNI_CALL_METHOD(Float, jfloat)
DEFINE_JNI_CALL_METHOD(Double, jdouble)
DEFINE_JNI_CALL_METHOD(Boolean, jboolean)

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_jni_JniHelperTest_callVoidMethod(
    JNIEnv* env, jclass, jclass clazz, jobject object, jstring name, jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jmethodID method_id = jni_helper.getMethodId(clazz, name_ptr.get(), sig_ptr.get());
  jni_helper.callVoidMethod(object, method_id);
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_callObjectMethod(JNIEnv* env, jclass, jclass clazz,
                                                                  jobject object, jstring name,
                                                                  jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jmethodID method_id = jni_helper.getMethodId(clazz, name_ptr.get(), sig_ptr.get());
  return jni_helper.callObjectMethod(object, method_id).release();
}

#define DEFINE_JNI_CALL_STATIC_METHOD(JAVA_TYPE, JNI_TYPE)                                         \
  extern "C" JNIEXPORT JNI_TYPE JNICALL                                                            \
      Java_io_envoyproxy_envoymobile_jni_JniHelperTest_callStatic##JAVA_TYPE##Method(              \
          JNIEnv* env, jclass, jclass clazz, jstring name, jstring signature) {                    \
    Envoy::JNI::JniHelper jni_helper(env);                                                         \
    Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);         \
    Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);     \
    jmethodID method_id = jni_helper.getStaticMethodId(clazz, name_ptr.get(), sig_ptr.get());      \
    return jni_helper.callStatic##JAVA_TYPE##Method(clazz, method_id);                             \
  }

DEFINE_JNI_CALL_STATIC_METHOD(Byte, jbyte)
DEFINE_JNI_CALL_STATIC_METHOD(Char, jchar)
DEFINE_JNI_CALL_STATIC_METHOD(Short, jshort)
DEFINE_JNI_CALL_STATIC_METHOD(Int, jint)
DEFINE_JNI_CALL_STATIC_METHOD(Long, jlong)
DEFINE_JNI_CALL_STATIC_METHOD(Float, jfloat)
DEFINE_JNI_CALL_STATIC_METHOD(Double, jdouble)
DEFINE_JNI_CALL_STATIC_METHOD(Boolean, jboolean)

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_callStaticVoidMethod(JNIEnv* env, jclass,
                                                                      jclass clazz, jstring name,
                                                                      jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jmethodID method_id = jni_helper.getStaticMethodId(clazz, name_ptr.get(), sig_ptr.get());
  jni_helper.callStaticVoidMethod(clazz, method_id);
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_jni_JniHelperTest_callStaticObjectMethod(JNIEnv* env, jclass,
                                                                        jclass clazz, jstring name,
                                                                        jstring signature) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr name_ptr = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::JNI::StringUtfUniquePtr sig_ptr = jni_helper.getStringUtfChars(signature, nullptr);
  jmethodID method_id = jni_helper.getStaticMethodId(clazz, name_ptr.get(), sig_ptr.get());
  return jni_helper.callStaticObjectMethod(clazz, method_id).release();
}
