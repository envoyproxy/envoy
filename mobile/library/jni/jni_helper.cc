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
// `jclass_cache_map` contains `jclass` references that are statically populated. This field is
// used by `FindClass` to find the `jclass` reference from a given class name.
absl::flat_hash_map<absl::string_view, jclass> jclass_cache_map;
absl::flat_hash_map<
    std::tuple<jclass, absl::string_view /* method */, absl::string_view /* signature */>,
    jmethodID>
    jmethod_id_cache_map;
absl::flat_hash_map<std::tuple<jclass, absl::string_view /* method */, absl::string_view>,
                    jmethodID /* signature */>
    static_jmethod_id_cache_map;
absl::flat_hash_map<std::tuple<jclass, absl::string_view /* field */, absl::string_view>,
                    jfieldID /* signature */>
    jfield_id_cache_map;
absl::flat_hash_map<std::tuple<jclass, absl::string_view /* field */, absl::string_view>,
                    jfieldID /* signature */>
    static_jfield_id_cache_map;
} // namespace

void GlobalRefDeleter::operator()(jobject object) const {
  if (object != nullptr) {
    JniHelper::getThreadLocalEnv()->DeleteGlobalRef(object);
  }
}

void LocalRefDeleter::operator()(jobject object) const {
  if (object != nullptr) {
    JniHelper::getThreadLocalEnv()->DeleteLocalRef(object);
  }
}

void StringUtfDeleter::operator()(const char* c_str) const {
  if (c_str != nullptr) {
    JniHelper::getThreadLocalEnv()->ReleaseStringUTFChars(j_str_, c_str);
  }
}

void PrimitiveArrayCriticalDeleter::operator()(void* c_array) const {
  if (c_array != nullptr) {
    JniHelper::getThreadLocalEnv()->ReleasePrimitiveArrayCritical(array_, c_array, 0);
  }
}

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
  for (const auto& [_, clazz] : jclass_cache_map) {
    env->DeleteGlobalRef(clazz);
  }
}

void JniHelper::addToCache(absl::string_view class_name, const std::vector<Method>& methods,
                           const std::vector<Method>& static_methods,
                           const std::vector<Field>& fields,
                           const std::vector<Field>& static_fields) {
  JNIEnv* env;
  jint result = getJavaVm()->GetEnv(reinterpret_cast<void**>(&env), getVersion());
  ASSERT(result == JNI_OK, "Unable to get JNIEnv from the JavaVM.");
  jclass java_class = env->FindClass(class_name.data());
  ASSERT(java_class != nullptr, absl::StrFormat("Unable to find class '%s'.", class_name));
  jclass java_class_global_ref = reinterpret_cast<jclass>(env->NewGlobalRef(java_class));
  jclass_cache_map.emplace(class_name, java_class_global_ref);

  for (const auto& [method_name, signature] : methods) {
    jmethodID method_id =
        env->GetMethodID(java_class_global_ref, method_name.data(), signature.data());
    ASSERT(method_id != nullptr,
           absl::StrFormat("Unable to find method ID for class '%s', method '%s', signature '%s'.",
                           class_name, method_name, signature));
    jmethod_id_cache_map.emplace(std::tuple<jclass, absl::string_view, absl::string_view>(
                                     java_class_global_ref, method_name, signature),
                                 method_id);
  }

  for (const auto& [method_name, signature] : static_methods) {
    jmethodID method_id =
        env->GetStaticMethodID(java_class_global_ref, method_name.data(), signature.data());
    ASSERT(method_id != nullptr,
           absl::StrFormat(
               "Unable to find static method ID for class '%s', method '%s', signature '%s'.",
               class_name, method_name, signature));
    static_jmethod_id_cache_map.emplace(std::tuple<jclass, absl::string_view, absl::string_view>(
                                            java_class_global_ref, method_name, signature),
                                        method_id);
  }

  for (const auto& [field_name, signature] : fields) {
    jfieldID field_id = env->GetFieldID(java_class_global_ref, field_name.data(), signature.data());
    ASSERT(field_id != nullptr,
           absl::StrFormat("Unable to find field ID for class '%s', field '%s', signature '%s'.",
                           class_name, field_name, signature));
    jfield_id_cache_map.emplace(std::tuple<jclass, absl::string_view, absl::string_view>(
                                    java_class_global_ref, field_name, signature),
                                field_id);
  }

  for (const auto& [field_name, signature] : static_fields) {
    jfieldID field_id =
        env->GetStaticFieldID(java_class_global_ref, field_name.data(), signature.data());
    ASSERT(field_id != nullptr,
           absl::StrFormat(
               "Unable to find static field ID for class '%s', field '%s', signature '%s'.",
               class_name, field_name, signature));
    static_jfield_id_cache_map.emplace(std::tuple<jclass, absl::string_view, absl::string_view>(
                                           java_class_global_ref, field_name, signature),
                                       field_id);
  }
}

JavaVM* JniHelper::getJavaVm() { return java_vm_cache_.load(std::memory_order_acquire); }

void JniHelper::detachCurrentThread() {
  ASSERT(getJavaVm()->DetachCurrentThread() == JNI_OK, "Unable to detach current thread.");
}

JNIEnv* JniHelper::getThreadLocalEnv() {
  JavaVM* java_vm = getJavaVm();
  ASSERT(java_vm != nullptr, "Unable to get JavaVM.");
  jint result = java_vm->GetEnv(reinterpret_cast<void**>(&jni_env_cache_), getVersion());
  if (result == JNI_EDETACHED) {
    JavaVMAttachArgs args = {getVersion(), const_cast<char*>(THREAD_NAME), nullptr};
#if defined(__ANDROID__)
    result = java_vm->AttachCurrentThreadAsDaemon(&jni_env_cache_, &args);
#else
    result = java_vm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&jni_env_cache_), &args);
#endif
  }
  ASSERT(result == JNI_OK, "Unable to get JNIEnv.");
  return jni_env_cache_;
}

JNIEnv* JniHelper::getEnv() { return env_; }

jfieldID JniHelper::getFieldId(jclass clazz, const char* name, const char* signature) {
  jfieldID field_id = env_->GetFieldID(clazz, name, signature);
  rethrowException();
  return field_id;
}

jfieldID JniHelper::getFieldIdFromCache(jclass clazz, const char* name, const char* signature) {
  if (auto it = jfield_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != jfield_id_cache_map.end()) {
    return it->second;
  }
  // In the debug mode, the code will fail if the field ID is not in the cache since this is most
  // likely due to a bug in the code. In the release mode, the code will use the non-caching field
  // ID.
  ASSERT(false, absl::StrFormat("Unable to find field ID '%s', signature '%s' from the cache.",
                                name, signature));
  return getFieldId(clazz, name, signature);
}

jfieldID JniHelper::getStaticFieldId(jclass clazz, const char* name, const char* signature) {
  jfieldID field_id = env_->GetStaticFieldID(clazz, name, signature);
  rethrowException();
  return field_id;
}

jfieldID JniHelper::getStaticFieldIdFromCache(jclass clazz, const char* name,
                                              const char* signature) {
  if (auto it = static_jfield_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != static_jfield_id_cache_map.end()) {
    return it->second;
  }
  // In the debug mode, the code will fail if the static field ID is not in the cache since this is
  // most likely due to a bug in the code. In the release mode, the code will use the non-caching
  // static field ID.
  ASSERT(false,
         absl::StrFormat("Unable to find static field ID '%s', signature '%s' from the cache.",
                         name, signature));
  return getStaticFieldId(clazz, name, signature);
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
  jmethodID method_id = env_->GetMethodID(clazz, name, signature);
  rethrowException();
  return method_id;
}

jmethodID JniHelper::getMethodIdFromCache(jclass clazz, const char* name, const char* signature) {
  if (auto it = jmethod_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != jmethod_id_cache_map.end()) {
    return it->second;
  }
  // In the debug mode, the code will fail if the method ID is not in the cache since this is most
  // likely due to a bug in the code. In the release mode, the code will use the non-caching method
  // ID.
  ASSERT(false, absl::StrFormat("Unable to find method ID '%s', signature '%s' from the cache.",
                                name, signature));
  return getMethodId(clazz, name, signature);
}

jmethodID JniHelper::getStaticMethodId(jclass clazz, const char* name, const char* signature) {
  jmethodID method_id = env_->GetStaticMethodID(clazz, name, signature);
  rethrowException();
  return method_id;
}

jmethodID JniHelper::getStaticMethodIdFromCache(jclass clazz, const char* name,
                                                const char* signature) {
  if (auto it = static_jmethod_id_cache_map.find(
          std::tuple<jclass, absl::string_view, absl::string_view>(clazz, name, signature));
      it != static_jmethod_id_cache_map.end()) {
    return it->second;
  }
  // In the debug mode, the code will fail if the static method ID is not in the cache since this is
  // most likely due to a bug in the code. In the release mode, the code will use the non-caching
  // static method ID.
  ASSERT(false,
         absl::StrFormat("Unable to find static method ID '%s', signature '%s' from the cache.",
                         name, signature));
  return getStaticMethodId(clazz, name, signature);
}

jclass JniHelper::findClassFromCache(const char* class_name) {
  if (auto i = jclass_cache_map.find(class_name); i != jclass_cache_map.end()) {
    return i->second;
  }
  // In the debug mode, the code will fail if the class is not in the cache since this is most
  // likely due to a bug in the code. In the release mode, the code will use the non-caching class.
  ASSERT(false, absl::StrFormat("Unable to find class '%s'.", class_name));
  return env_->FindClass(class_name);
}

LocalRefUniquePtr<jclass> JniHelper::getObjectClass(jobject object) {
  return {env_->GetObjectClass(object), LocalRefDeleter()};
}

void JniHelper::throwNew(const char* java_class_name, const char* message) {
  jclass java_class = findClassFromCache(java_class_name);
  if (java_class != nullptr) {
    jint error = env_->ThrowNew(java_class, message);
    ASSERT(error == JNI_OK, "Failed calling ThrowNew.");
  }
}

jboolean JniHelper::exceptionCheck() { return env_->ExceptionCheck(); }

LocalRefUniquePtr<jthrowable> JniHelper::exceptionOccurred() {
  return {env_->ExceptionOccurred(), LocalRefDeleter()};
}

void JniHelper::exceptionCleared() { env_->ExceptionClear(); }

GlobalRefUniquePtr<jobject> JniHelper::newGlobalRef(jobject object) {
  GlobalRefUniquePtr<jobject> result(env_->NewGlobalRef(object), GlobalRefDeleter());
  return result;
}

LocalRefUniquePtr<jobject> JniHelper::newObject(jclass clazz, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  LocalRefUniquePtr<jobject> result(env_->NewObjectV(clazz, method_id, args), LocalRefDeleter());
  rethrowException();
  va_end(args);
  return result;
}

LocalRefUniquePtr<jstring> JniHelper::newStringUtf(const char* str) {
  LocalRefUniquePtr<jstring> result(env_->NewStringUTF(str), LocalRefDeleter());
  rethrowException();
  return result;
}

StringUtfUniquePtr JniHelper::getStringUtfChars(jstring str, jboolean* is_copy) {
  StringUtfUniquePtr result(env_->GetStringUTFChars(str, is_copy), StringUtfDeleter(str));
  rethrowException();
  return result;
}

jsize JniHelper::getArrayLength(jarray array) { return env_->GetArrayLength(array); }

#define DEFINE_NEW_ARRAY(JAVA_TYPE, JNI_TYPE)                                                      \
  LocalRefUniquePtr<JNI_TYPE> JniHelper::new##JAVA_TYPE##Array(jsize length) {                     \
    LocalRefUniquePtr<JNI_TYPE> result(env_->New##JAVA_TYPE##Array(length), LocalRefDeleter());    \
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
      env_->NewObjectArray(length, element_class, initial_element), LocalRefDeleter());

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
                                    LocalRefDeleter());
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
