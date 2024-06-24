#include "library/jni/jni_helper.h"

#include "source/common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace JNI {
namespace {
// Const variables.
constexpr jint JNI_VERSION = JNI_VERSION_1_6;
constexpr const char* THREAD_NAME = "EnvoyMain";
// Non-const variables.
std::atomic<JavaVM*> java_vm_cache_;
thread_local JNIEnv* jni_env_cache_ = nullptr;
absl::flat_hash_map<absl::string_view, jclass> jclass_cache_map;
// The `jclass_cache_set` is a superset of `jclass_cache_map`. It contains `jclass` objects that are
// retrieve dynamically via `GetObjectClass`.
thread_local absl::flat_hash_set<jclass> jclass_cache_set;
thread_local absl::flat_hash_map<
    std::tuple<jclass, absl::string_view /* method */, absl::string_view /* signature */>,
    jmethodID>
    jmethod_id_cache_map;
thread_local absl::flat_hash_map<
    std::tuple<jclass, absl::string_view /* method */, absl::string_view>,
    jmethodID /* signature */>
    static_jmethod_id_cache_map;
thread_local absl::flat_hash_map<
    std::tuple<jclass, absl::string_view /* field */, absl::string_view>, jfieldID /* signature */>
    jfield_id_cache_map;
thread_local absl::flat_hash_map<
    std::tuple<jclass, absl::string_view /* field */, absl::string_view>, jfieldID /* signature */>
    static_jfield_id_cache_map;

/**
 * This function checks if the `clazz` already exists in the `jclass` `GlobalRef` cache and creates
 * a new `GlobalRef` if it does not already exist. This functions returns the `GlobalRef` of the
 * specified `clazz`.
 */
jclass addClassToCacheIfNotExist(JNIEnv* env, jclass clazz) {
  jclass java_class_global_ref = clazz;
  if (auto it = jclass_cache_set.find(clazz); it == jclass_cache_set.end()) {
    jclass global_ref = reinterpret_cast<jclass>(env->NewGlobalRef(clazz));
    jclass_cache_set.emplace(global_ref);
  }
  return java_class_global_ref;
}
} // namespace

jint JniHelper::getVersion() { return JNI_VERSION; }

void JniHelper::initialize(JavaVM* java_vm) {
  java_vm_cache_.store(java_vm, std::memory_order_release);
}

void JniHelper::finalize() {
  JNIEnv* env;
  jint result = getJavaVm()->GetEnv(reinterpret_cast<void**>(&env), getVersion());
  ASSERT(result == JNI_OK, "Unable to get JNIEnv from the JavaVM.");
  // Clear the caches and delete the global references.
  jmethod_id_cache_map.clear();
  static_jmethod_id_cache_map.clear();
  jfield_id_cache_map.clear();
  static_jfield_id_cache_map.clear();
  jclass_cache_map.clear();
  for (const auto& clazz : jclass_cache_set) {
    env->DeleteGlobalRef(clazz);
  }
  jclass_cache_set.clear();
}

void JniHelper::addClassToCache(const char* class_name) {
  JNIEnv* env;
  jint result = getJavaVm()->GetEnv(reinterpret_cast<void**>(&env), getVersion());
  ASSERT(result == JNI_OK, "Unable to get JNIEnv from the JavaVM.");
  jclass java_class = env->FindClass(class_name);
  ASSERT(java_class != nullptr, absl::StrFormat("Unable to find class '%s'.", class_name));
  jclass java_class_global_ref = reinterpret_cast<jclass>(env->NewGlobalRef(java_class));
  jclass_cache_map.emplace(class_name, java_class_global_ref);
  jclass_cache_set.emplace(java_class_global_ref);
}

JavaVM* JniHelper::getJavaVm() { return java_vm_cache_.load(std::memory_order_acquire); }

void JniHelper::detachCurrentThread() {
  ASSERT(getJavaVm()->DetachCurrentThread() == JNI_OK, "Unable to detach current thread.");
}

JNIEnv* JniHelper::getThreadLocalEnv() {
  if (jni_env_cache_ != nullptr) {
    return jni_env_cache_;
  }
  JavaVM* java_vm = getJavaVm();
  ASSERT(java_vm != nullptr, "Unable to get JavaVM.");
  jint result = java_vm->GetEnv(reinterpret_cast<void**>(&jni_env_cache_), getVersion());
  if (result == JNI_EDETACHED) {
    JavaVMAttachArgs args = {getVersion(), const_cast<char*>(THREAD_NAME), nullptr};
#if defined(__ANDROID__)
    result = java_vm->AttachCurrentThread(&jni_env_cache_, &args);
#else
    result = java_vm->AttachCurrentThread(reinterpret_cast<void**>(&jni_env_cache_), &args);
#endif
  }
  ASSERT(result == JNI_OK, "Unable to get JNIEnv.");
  return jni_env_cache_;
}

JNIEnv* JniHelper::getEnv() { return env_; }

jfieldID JniHelper::getFieldId(jclass clazz, const char* name, const char* signature) {
  if (auto it = jfield_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != jfield_id_cache_map.end()) {
    return it->second;
  }
  jfieldID field_id = env_->GetFieldID(clazz, name, signature);
  jclass clazz_global_ref = addClassToCacheIfNotExist(env_, clazz);
  jfield_id_cache_map.emplace(
      std::tuple<jclass, absl::string_view, absl::string_view>(clazz_global_ref, name, signature),
      field_id);
  rethrowException();
  return field_id;
}

jfieldID JniHelper::getStaticFieldId(jclass clazz, const char* name, const char* signature) {
  if (auto it = static_jfield_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != static_jfield_id_cache_map.end()) {
    return it->second;
  }
  jfieldID field_id = env_->GetStaticFieldID(clazz, name, signature);
  jclass clazz_global_ref = addClassToCacheIfNotExist(env_, clazz);
  static_jfield_id_cache_map.emplace(
      std::tuple<jclass, absl::string_view, absl::string_view>(clazz_global_ref, name, signature),
      field_id);
  rethrowException();
  return field_id;
}

#define DEFINE_GET_FIELD(JAVA_TYPE, JNI_TYPE)                                                      \
  JNI_TYPE JniHelper::get##JAVA_TYPE##Field(jobject object, jfieldID field_id) {                   \
    return env_->Get##JAVA_TYPE##Field(object, field_id);                                          \
  }

DEFINE_GET_FIELD(Byte, jbyte)
DEFINE_GET_FIELD(Char, jchar)
DEFINE_GET_FIELD(Short, jshort)
DEFINE_GET_FIELD(Int, jint)
DEFINE_GET_FIELD(Long, jlong)
DEFINE_GET_FIELD(Float, jfloat)
DEFINE_GET_FIELD(Double, jdouble)
DEFINE_GET_FIELD(Boolean, jboolean)

jmethodID JniHelper::getMethodId(jclass clazz, const char* name, const char* signature) {
  if (auto it = jmethod_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != jmethod_id_cache_map.end()) {
    return it->second;
  }
  jmethodID method_id = env_->GetMethodID(clazz, name, signature);
  jclass clazz_global_ref = addClassToCacheIfNotExist(env_, clazz);
  jmethod_id_cache_map.emplace(
      std::tuple<jclass, absl::string_view, absl::string_view>(clazz_global_ref, name, signature),
      method_id);
  rethrowException();
  return method_id;
}

jmethodID JniHelper::getStaticMethodId(jclass clazz, const char* name, const char* signature) {
  if (auto it = static_jmethod_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != static_jmethod_id_cache_map.end()) {
    return it->second;
  }
  jmethodID method_id = env_->GetStaticMethodID(clazz, name, signature);
  jclass clazz_global_ref = addClassToCacheIfNotExist(env_, clazz);
  static_jmethod_id_cache_map.emplace(
      std::tuple<jclass, absl::string_view, absl::string_view>(clazz_global_ref, name, signature),
      method_id);
  rethrowException();
  return method_id;
}

jclass JniHelper::findClass(const char* class_name) {
  if (auto i = jclass_cache_map.find(class_name); i != jclass_cache_map.end()) {
    return i->second;
  }
  ASSERT(false, absl::StrFormat("Unable to find class '%s'.", class_name));
  return nullptr;
}

LocalRefUniquePtr<jclass> JniHelper::getObjectClass(jobject object) {
  return {env_->GetObjectClass(object), LocalRefDeleter(env_)};
}

void JniHelper::throwNew(const char* java_class_name, const char* message) {
  jclass java_class = findClass(java_class_name);
  if (java_class != nullptr) {
    jint error = env_->ThrowNew(java_class, message);
    ASSERT(error == JNI_OK, "Failed calling ThrowNew.");
  }
}

jboolean JniHelper::exceptionCheck() { return env_->ExceptionCheck(); }

LocalRefUniquePtr<jthrowable> JniHelper::exceptionOccurred() {
  return {env_->ExceptionOccurred(), LocalRefDeleter(env_)};
}

void JniHelper::exceptionCleared() { env_->ExceptionClear(); }

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

LocalRefUniquePtr<jobject> JniHelper::newDirectByteBuffer(void* address, jlong capacity) {
  LocalRefUniquePtr<jobject> result(env_->NewDirectByteBuffer(address, capacity),
                                    LocalRefDeleter(env_));
  rethrowException();
  return result;
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
