#include "library/common/jni/jni_helper.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace JNI {

JNIEnv* JniHelper::getEnv() { return env_; }

jmethodID JniHelper::getMethodId(jclass clazz, const char* name, const char* signature) {
  jmethodID method_id = env_->GetMethodID(clazz, name, signature);
  rethrowException();
  return method_id;
}

jmethodID JniHelper::getStaticMethodId(jclass clazz, const char* name, const char* signature) {
  jmethodID method_id = env_->GetStaticMethodID(clazz, name, signature);
  rethrowException();
  return method_id;
}

LocalRefUniquePtr<jclass> JniHelper::findClass(const char* class_name) {
  LocalRefUniquePtr<jclass> result(env_->FindClass(class_name), LocalRefDeleter(env_));
  rethrowException();
  return result;
}

LocalRefUniquePtr<jclass> JniHelper::getObjectClass(jobject object) {
  return {env_->GetObjectClass(object), LocalRefDeleter(env_)};
}

void JniHelper::throwNew(const char* java_class_name, const char* message) {
  LocalRefUniquePtr<jclass> java_class = findClass(java_class_name);
  if (java_class != nullptr) {
    jint error = env_->ThrowNew(java_class.get(), message);
    RELEASE_ASSERT(error == JNI_OK, fmt::format("Failed calling ThrowNew."));
  }
}

LocalRefUniquePtr<jthrowable> JniHelper::exceptionOccurred() {
  return {env_->ExceptionOccurred(), LocalRefDeleter(env_)};
}

GlobalRefUniquePtr<jobject> JniHelper::newGlobalRef(jobject object) {
  GlobalRefUniquePtr<jobject> result(env_->NewGlobalRef(object), GlobalRefDeleter(env_));
  return result;
}

LocalRefUniquePtr<jobject> JniHelper::newObject(jclass clazz, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  LocalRefUniquePtr<jobject> result(env_->NewObjectV(clazz, method_id, args),
                                    LocalRefDeleter(env_));
  rethrowException();
  va_end(args);
  return result;
}

LocalRefUniquePtr<jstring> JniHelper::newStringUtf(const char* str) {
  LocalRefUniquePtr<jstring> result(env_->NewStringUTF(str), LocalRefDeleter(env_));
  rethrowException();
  return result;
}

StringUtfUniquePtr JniHelper::getStringUtfChars(jstring str, jboolean* is_copy) {
  StringUtfUniquePtr result(env_->GetStringUTFChars(str, is_copy), StringUtfDeleter(env_, str));
  rethrowException();
  return result;
}

jsize JniHelper::getArrayLength(jarray array) { return env_->GetArrayLength(array); }

#define DEFINE_NEW_ARRAY(JAVA_TYPE, JNI_TYPE)                                                      \
  LocalRefUniquePtr<JNI_TYPE> JniHelper::new##JAVA_TYPE##Array(jsize length) {                     \
    LocalRefUniquePtr<JNI_TYPE> result(env_->New##JAVA_TYPE##Array(length),                        \
                                       LocalRefDeleter(env_));                                     \
    rethrowException();                                                                            \
    return result;                                                                                 \
  }

DEFINE_NEW_ARRAY(Byte, jbyteArray)
DEFINE_NEW_ARRAY(Char, jcharArray)
DEFINE_NEW_ARRAY(Short, jshortArray)
DEFINE_NEW_ARRAY(Int, jintArray)
DEFINE_NEW_ARRAY(Long, jlongArray)
DEFINE_NEW_ARRAY(Float, jfloatArray)
DEFINE_NEW_ARRAY(Double, jdoubleArray)
DEFINE_NEW_ARRAY(Boolean, jbooleanArray)

LocalRefUniquePtr<jobjectArray> JniHelper::newObjectArray(jsize length, jclass element_class,
                                                          jobject initial_element) {
  LocalRefUniquePtr<jobjectArray> result(
      env_->NewObjectArray(length, element_class, initial_element), LocalRefDeleter(env_));

  return result;
}

#define DEFINE_GET_ARRAY_ELEMENTS(JAVA_TYPE, JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE)                     \
  ArrayElementsUniquePtr<JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE>                                         \
      JniHelper::get##JAVA_TYPE##ArrayElements(JNI_ARRAY_TYPE array, jboolean* is_copy) {          \
    ArrayElementsUniquePtr<JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE> result(                               \
        env_->Get##JAVA_TYPE##ArrayElements(array, is_copy),                                       \
        ArrayElementsDeleter<JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE>(env_, array));                      \
    rethrowException();                                                                            \
    return result;                                                                                 \
  }

DEFINE_GET_ARRAY_ELEMENTS(Byte, jbyteArray, jbyte)
DEFINE_GET_ARRAY_ELEMENTS(Char, jcharArray, jchar)
DEFINE_GET_ARRAY_ELEMENTS(Short, jshortArray, jshort)
DEFINE_GET_ARRAY_ELEMENTS(Int, jintArray, jint)
DEFINE_GET_ARRAY_ELEMENTS(Long, jlongArray, jlong)
DEFINE_GET_ARRAY_ELEMENTS(Float, jfloatArray, jfloat)
DEFINE_GET_ARRAY_ELEMENTS(Double, jdoubleArray, jdouble)
DEFINE_GET_ARRAY_ELEMENTS(Boolean, jbooleanArray, jboolean)

void JniHelper::setObjectArrayElement(jobjectArray array, jsize index, jobject value) {
  env_->SetObjectArrayElement(array, index, value);
  rethrowException();
}

#define DEFINE_SET_ARRAY_REGION(JAVA_TYPE, JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE)                       \
  void JniHelper::set##JAVA_TYPE##ArrayRegion(JNI_ARRAY_TYPE array, jsize start, jsize length,     \
                                              const JNI_ELEMENT_TYPE* buffer) {                    \
    env_->Set##JAVA_TYPE##ArrayRegion(array, start, length, buffer);                               \
    rethrowException();                                                                            \
  }

DEFINE_SET_ARRAY_REGION(Byte, jbyteArray, jbyte)
DEFINE_SET_ARRAY_REGION(Char, jcharArray, jchar)
DEFINE_SET_ARRAY_REGION(Short, jshortArray, jshort)
DEFINE_SET_ARRAY_REGION(Int, jintArray, jint)
DEFINE_SET_ARRAY_REGION(Long, jlongArray, jlong)
DEFINE_SET_ARRAY_REGION(Float, jfloatArray, jfloat)
DEFINE_SET_ARRAY_REGION(Double, jdoubleArray, jdouble)
DEFINE_SET_ARRAY_REGION(Boolean, jbooleanArray, jboolean)

#define DEFINE_CALL_METHOD(JAVA_TYPE, JNI_TYPE)                                                    \
  JNI_TYPE JniHelper::call##JAVA_TYPE##Method(jobject object, jmethodID method_id, ...) {          \
    va_list args;                                                                                  \
    va_start(args, method_id);                                                                     \
    JNI_TYPE result = env_->Call##JAVA_TYPE##MethodV(object, method_id, args);                     \
    va_end(args);                                                                                  \
    rethrowException();                                                                            \
    return result;                                                                                 \
  }

DEFINE_CALL_METHOD(Byte, jbyte)
DEFINE_CALL_METHOD(Char, jchar)
DEFINE_CALL_METHOD(Short, jshort)
DEFINE_CALL_METHOD(Int, jint)
DEFINE_CALL_METHOD(Long, jlong)
DEFINE_CALL_METHOD(Float, jfloat)
DEFINE_CALL_METHOD(Double, jdouble)
DEFINE_CALL_METHOD(Boolean, jboolean)

void JniHelper::callVoidMethod(jobject object, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  env_->CallVoidMethodV(object, method_id, args);
  va_end(args);
  rethrowException();
}

#define DEFINE_CALL_STATIC_METHOD(JAVA_TYPE, JNI_TYPE)                                             \
  JNI_TYPE JniHelper::callStatic##JAVA_TYPE##Method(jclass clazz, jmethodID method_id, ...) {      \
    va_list args;                                                                                  \
    va_start(args, method_id);                                                                     \
    JNI_TYPE result = env_->CallStatic##JAVA_TYPE##MethodV(clazz, method_id, args);                \
    va_end(args);                                                                                  \
    rethrowException();                                                                            \
    return result;                                                                                 \
  }

DEFINE_CALL_STATIC_METHOD(Byte, jbyte)
DEFINE_CALL_STATIC_METHOD(Char, jchar)
DEFINE_CALL_STATIC_METHOD(Short, jshort)
DEFINE_CALL_STATIC_METHOD(Int, jint)
DEFINE_CALL_STATIC_METHOD(Long, jlong)
DEFINE_CALL_STATIC_METHOD(Float, jfloat)
DEFINE_CALL_STATIC_METHOD(Double, jdouble)
DEFINE_CALL_STATIC_METHOD(Boolean, jboolean)

void JniHelper::callStaticVoidMethod(jclass clazz, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  env_->CallStaticVoidMethodV(clazz, method_id, args);
  va_end(args);
  rethrowException();
}

jlong JniHelper::getDirectBufferCapacity(jobject buffer) {
  return env_->GetDirectBufferCapacity(buffer);
}

void JniHelper::rethrowException() {
  if (env_->ExceptionCheck()) {
    auto throwable = exceptionOccurred();
    env_->ExceptionClear();
    env_->Throw(throwable.release());
  }
}

} // namespace JNI
} // namespace Envoy
