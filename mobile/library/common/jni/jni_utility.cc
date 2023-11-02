#include "library/common/jni/jni_utility.h"

#include <stdlib.h>
#include <string.h>

#include "source/common/common/assert.h"

#include "library/common/jni/jni_support.h"
#include "library/common/jni/types/env.h"
#include "library/common/jni/types/exception.h"

namespace Envoy {
namespace JNI {

static jobject static_class_loader = nullptr;

void set_class_loader(jobject class_loader) { static_class_loader = class_loader; }

jobject get_class_loader() {
  RELEASE_ASSERT(static_class_loader,
                 "find_class() is used before calling AndroidJniLibrary.load()");
  return static_class_loader;
}

jclass find_class(const char* class_name) {
  JNIEnv* env = get_env();
  jclass class_loader = env->FindClass("java/lang/ClassLoader");
  Envoy::JNI::Exception::checkAndClear("find_class:FindClass");
  jmethodID find_class_method =
      env->GetMethodID(class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  Envoy::JNI::Exception::checkAndClear("find_class:GetMethodID");
  jstring str_class_name = env->NewStringUTF(class_name);
  Envoy::JNI::Exception::checkAndClear("find_class:NewStringUTF");
  jclass clazz =
      (jclass)(env->CallObjectMethod(get_class_loader(), find_class_method, str_class_name));
  Envoy::JNI::Exception::checkAndClear("find_class:CallObjectMethod");
  env->DeleteLocalRef(str_class_name);
  return clazz;
}

JNIEnv* get_env() { return Envoy::JNI::Env::get(); }

void jni_delete_global_ref(void* context) {
  JNIEnv* env = get_env();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

void jni_delete_const_global_ref(const void* context) {
  jni_delete_global_ref(const_cast<void*>(context));
}

int unbox_integer(JniHelper& jni_helper, jobject boxedInteger) {
  jclass jcls_Integer = jni_helper.getEnv()->FindClass("java/lang/Integer");
  jmethodID jmid_intValue = jni_helper.getEnv()->GetMethodID(jcls_Integer, "intValue", "()I");
  jni_helper.getEnv()->DeleteLocalRef(jcls_Integer);
  return callIntMethod(jni_helper, boxedInteger, jmid_intValue);
}

envoy_data array_to_native_data(JniHelper& jni_helper, jbyteArray j_data) {
  size_t data_length = static_cast<size_t>(jni_helper.getEnv()->GetArrayLength(j_data));
  return array_to_native_data(jni_helper, j_data, data_length);
}

envoy_data array_to_native_data(JniHelper& jni_helper, jbyteArray j_data, size_t data_length) {
  uint8_t* native_bytes = static_cast<uint8_t*>(safe_malloc(data_length));
  void* critical_data = jni_helper.getEnv()->GetPrimitiveArrayCritical(j_data, 0);
  memcpy(native_bytes, critical_data, data_length); // NOLINT(safe-memcpy)
  jni_helper.getEnv()->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return {data_length, native_bytes, free, native_bytes};
}

jstring native_data_to_string(JniHelper& jni_helper, envoy_data data) {
  // Ensure we get a null-terminated string, the data coming in via envoy_data might not be.
  std::string str(reinterpret_cast<const char*>(data.bytes), data.length);
  jstring jstrBuf = jni_helper.getEnv()->NewStringUTF(str.c_str());
  return jstrBuf;
}

jbyteArray native_data_to_array(JniHelper& jni_helper, envoy_data data) {
  jbyteArray j_data = jni_helper.getEnv()->NewByteArray(data.length);
  void* critical_data = jni_helper.getEnv()->GetPrimitiveArrayCritical(j_data, nullptr);
  RELEASE_ASSERT(critical_data != nullptr, "unable to allocate memory in jni_utility");
  memcpy(critical_data, data.bytes, data.length); // NOLINT(safe-memcpy)
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  // TODO: potential perf improvement. Check if copied via isCopy, and optimize memory handling.
  jni_helper.getEnv()->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return j_data;
}

jlongArray native_stream_intel_to_array(JniHelper& jni_helper, envoy_stream_intel stream_intel) {
  jlongArray j_array = jni_helper.getEnv()->NewLongArray(4);
  jlong* critical_array =
      static_cast<jlong*>(jni_helper.getEnv()->GetPrimitiveArrayCritical(j_array, nullptr));
  RELEASE_ASSERT(critical_array != nullptr, "unable to allocate memory in jni_utility");
  critical_array[0] = static_cast<jlong>(stream_intel.stream_id);
  critical_array[1] = static_cast<jlong>(stream_intel.connection_id);
  critical_array[2] = static_cast<jlong>(stream_intel.attempt_count);
  critical_array[3] = static_cast<jlong>(stream_intel.consumed_bytes_from_response);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  jni_helper.getEnv()->ReleasePrimitiveArrayCritical(j_array, critical_array, 0);
  return j_array;
}

jlongArray native_final_stream_intel_to_array(JniHelper& jni_helper,
                                              envoy_final_stream_intel final_stream_intel) {
  jlongArray j_array = jni_helper.getEnv()->NewLongArray(16);
  jlong* critical_array =
      static_cast<jlong*>(jni_helper.getEnv()->GetPrimitiveArrayCritical(j_array, nullptr));
  RELEASE_ASSERT(critical_array != nullptr, "unable to allocate memory in jni_utility");

  critical_array[0] = static_cast<jlong>(final_stream_intel.stream_start_ms);
  critical_array[1] = static_cast<jlong>(final_stream_intel.dns_start_ms);
  critical_array[2] = static_cast<jlong>(final_stream_intel.dns_end_ms);
  critical_array[3] = static_cast<jlong>(final_stream_intel.connect_start_ms);
  critical_array[4] = static_cast<jlong>(final_stream_intel.connect_end_ms);
  critical_array[5] = static_cast<jlong>(final_stream_intel.ssl_start_ms);
  critical_array[6] = static_cast<jlong>(final_stream_intel.ssl_end_ms);
  critical_array[7] = static_cast<jlong>(final_stream_intel.sending_start_ms);
  critical_array[8] = static_cast<jlong>(final_stream_intel.sending_end_ms);
  critical_array[9] = static_cast<jlong>(final_stream_intel.response_start_ms);
  critical_array[10] = static_cast<jlong>(final_stream_intel.stream_end_ms);
  critical_array[11] = static_cast<jlong>(final_stream_intel.socket_reused);
  critical_array[12] = static_cast<jlong>(final_stream_intel.sent_byte_count);
  critical_array[13] = static_cast<jlong>(final_stream_intel.received_byte_count);
  critical_array[14] = static_cast<jlong>(final_stream_intel.response_flags);
  critical_array[15] = static_cast<jlong>(final_stream_intel.upstream_protocol);

  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  jni_helper.getEnv()->ReleasePrimitiveArrayCritical(j_array, critical_array, 0);
  return j_array;
}

jobject native_map_to_map(JniHelper& jni_helper, envoy_map map) {
  jclass jcls_hashMap = jni_helper.getEnv()->FindClass("java/util/HashMap");
  jmethodID jmid_hashMapInit = jni_helper.getEnv()->GetMethodID(jcls_hashMap, "<init>", "(I)V");
  jobject j_hashMap = jni_helper.getEnv()->NewObject(jcls_hashMap, jmid_hashMapInit, map.length);
  jmethodID jmid_hashMapPut = jni_helper.getEnv()->GetMethodID(
      jcls_hashMap, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    auto key = native_data_to_string(jni_helper, map.entries[i].key);
    auto value = native_data_to_string(jni_helper, map.entries[i].value);
    callObjectMethod(jni_helper, j_hashMap, jmid_hashMapPut, key, value);
    jni_helper.getEnv()->DeleteLocalRef(key);
    jni_helper.getEnv()->DeleteLocalRef(value);
  }
  jni_helper.getEnv()->DeleteLocalRef(jcls_hashMap);
  return j_hashMap;
}

envoy_data buffer_to_native_data(JniHelper& jni_helper, jobject j_data) {
  // Returns -1 if the buffer is not a direct buffer.
  jlong data_length = jni_helper.getEnv()->GetDirectBufferCapacity(j_data);

  if (data_length < 0) {
    jclass jcls_ByteBuffer = jni_helper.getEnv()->FindClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getEnv()->GetMethodID(jcls_ByteBuffer, "array", "()[B");
    jbyteArray array = static_cast<jbyteArray>(callObjectMethod(jni_helper, j_data, jmid_array));
    jni_helper.getEnv()->DeleteLocalRef(jcls_ByteBuffer);

    envoy_data native_data = array_to_native_data(jni_helper, array);
    jni_helper.getEnv()->DeleteLocalRef(array);
    return native_data;
  }

  return buffer_to_native_data(jni_helper, j_data, static_cast<size_t>(data_length));
}

envoy_data buffer_to_native_data(JniHelper& jni_helper, jobject j_data, size_t data_length) {
  // Returns nullptr if the buffer is not a direct buffer.
  uint8_t* direct_address =
      static_cast<uint8_t*>(jni_helper.getEnv()->GetDirectBufferAddress(j_data));

  if (direct_address == nullptr) {
    jclass jcls_ByteBuffer = jni_helper.getEnv()->FindClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getEnv()->GetMethodID(jcls_ByteBuffer, "array", "()[B");
    jbyteArray array = static_cast<jbyteArray>(callObjectMethod(jni_helper, j_data, jmid_array));
    jni_helper.getEnv()->DeleteLocalRef(jcls_ByteBuffer);

    envoy_data native_data = array_to_native_data(jni_helper, array, data_length);
    jni_helper.getEnv()->DeleteLocalRef(array);
    return native_data;
  }

  envoy_data native_data;
  native_data.bytes = direct_address;
  native_data.length = data_length;
  native_data.release = jni_delete_global_ref;
  native_data.context = jni_helper.getEnv()->NewGlobalRef(j_data);

  return native_data;
}

envoy_data* buffer_to_native_data_ptr(JniHelper& jni_helper, jobject j_data) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (j_data == nullptr) {
    return nullptr;
  }

  envoy_data* native_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_data = buffer_to_native_data(jni_helper, j_data);
  return native_data;
}

envoy_headers to_native_headers(JniHelper& jni_helper, jobjectArray headers) {
  return to_native_map(jni_helper, headers);
}

envoy_headers* to_native_headers_ptr(JniHelper& jni_helper, jobjectArray headers) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (headers == nullptr) {
    return nullptr;
  }

  envoy_headers* native_headers = static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_headers = to_native_headers(jni_helper, headers);
  return native_headers;
}

envoy_stats_tags to_native_tags(JniHelper& jni_helper, jobjectArray tags) {
  return to_native_map(jni_helper, tags);
}

envoy_map to_native_map(JniHelper& jni_helper, jobjectArray entries) {
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_map_entry.
  envoy_map_size_t length = jni_helper.getEnv()->GetArrayLength(entries);
  if (length == 0) {
    return {0, nullptr};
  }

  envoy_map_entry* entry_array =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * length / 2));

  for (envoy_map_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    jbyteArray j_key =
        static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(entries, i));
    envoy_data entry_key = array_to_native_data(jni_helper, j_key);

    // Copy native byte array for header value
    jbyteArray j_value =
        static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(entries, i + 1));
    envoy_data entry_value = array_to_native_data(jni_helper, j_value);

    entry_array[i / 2] = {entry_key, entry_value};
    jni_helper.getEnv()->DeleteLocalRef(j_key);
    jni_helper.getEnv()->DeleteLocalRef(j_value);
  }

  envoy_map native_map = {length / 2, entry_array};
  return native_map;
}

jobjectArray ToJavaArrayOfObjectArray(JniHelper& jni_helper,
                                      const Envoy::Types::ManagedEnvoyHeaders& map) {
  jclass jcls_byte_array = jni_helper.getEnv()->FindClass("java/lang/Object");
  jobjectArray javaArray =
      jni_helper.getEnv()->NewObjectArray(2 * map.get().length, jcls_byte_array, nullptr);

  for (envoy_map_size_t i = 0; i < map.get().length; i++) {
    jbyteArray key = native_data_to_array(jni_helper, map.get().entries[i].key);
    jbyteArray value = native_data_to_array(jni_helper, map.get().entries[i].value);

    jni_helper.getEnv()->SetObjectArrayElement(javaArray, 2 * i, key);
    jni_helper.getEnv()->SetObjectArrayElement(javaArray, 2 * i + 1, value);
  }

  return javaArray;
}

jobjectArray ToJavaArrayOfByteArray(JniHelper& jni_helper, const std::vector<std::string>& v) {
  jclass jcls_byte_array = jni_helper.getEnv()->FindClass("[B");
  jobjectArray joa = jni_helper.getEnv()->NewObjectArray(v.size(), jcls_byte_array, nullptr);

  for (size_t i = 0; i < v.size(); ++i) {
    jbyteArray byte_array =
        ToJavaByteArray(jni_helper, reinterpret_cast<const uint8_t*>(v[i].data()), v[i].length());
    jni_helper.getEnv()->SetObjectArrayElement(joa, i, byte_array);
  }
  return joa;
}

jbyteArray ToJavaByteArray(JniHelper& jni_helper, const uint8_t* bytes, size_t len) {
  jbyteArray byte_array = jni_helper.getEnv()->NewByteArray(len);
  const jbyte* jbytes = reinterpret_cast<const jbyte*>(bytes);
  jni_helper.getEnv()->SetByteArrayRegion(byte_array, /*start=*/0, len, jbytes);
  return byte_array;
}

jbyteArray ToJavaByteArray(JniHelper& jni_helper, const std::string& str) {
  const uint8_t* str_bytes = reinterpret_cast<const uint8_t*>(str.data());
  return ToJavaByteArray(jni_helper, str_bytes, str.size());
}

void JavaArrayOfByteArrayToStringVector(JniHelper& jni_helper, jobjectArray array,
                                        std::vector<std::string>* out) {
  ASSERT(out);
  ASSERT(array);
  size_t len = jni_helper.getEnv()->GetArrayLength(array);
  out->resize(len);

  for (size_t i = 0; i < len; ++i) {
    jbyteArray bytes_array =
        static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(array, i));
    jsize bytes_len = jni_helper.getEnv()->GetArrayLength(bytes_array);
    // It doesn't matter if the array returned by GetByteArrayElements is a copy
    // or not, as the data will be simply be copied into C++ owned memory below.
    jbyte* bytes = jni_helper.getEnv()->GetByteArrayElements(bytes_array, /*isCopy=*/nullptr);
    (*out)[i].assign(reinterpret_cast<const char*>(bytes), bytes_len);
    // There is nothing to write back, it is always safe to JNI_ABORT.
    jni_helper.getEnv()->ReleaseByteArrayElements(bytes_array, bytes, JNI_ABORT);
    // Explicitly delete to keep the local ref count low.
    jni_helper.getEnv()->DeleteLocalRef(bytes_array);
  }
}

void JavaArrayOfByteToString(JniHelper& jni_helper, jbyteArray jbytes, std::string* out) {
  std::vector<uint8_t> bytes;
  JavaArrayOfByteToBytesVector(jni_helper, jbytes, &bytes);
  *out = std::string(bytes.begin(), bytes.end());
}

void JavaArrayOfByteToBytesVector(JniHelper& jni_helper, jbyteArray array,
                                  std::vector<uint8_t>* out) {
  const size_t len = jni_helper.getEnv()->GetArrayLength(array);
  out->resize(len);

  // It doesn't matter if the array returned by GetByteArrayElements is a copy
  // or not, as the data will be simply be copied into C++ owned memory below.
  jbyte* jbytes = jni_helper.getEnv()->GetByteArrayElements(array, /*isCopy=*/nullptr);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(jbytes);
  std::copy(bytes, bytes + len, out->begin());
  // There is nothing to write back, it is always safe to JNI_ABORT.
  jni_helper.getEnv()->ReleaseByteArrayElements(array, jbytes, JNI_ABORT);
}

MatcherData::Type StringToType(std::string type_as_string) {
  if (type_as_string.length() != 4) {
    ASSERT("conversion failure failure");
    return MatcherData::EXACT;
  }
  // grab the lowest bit.
  switch (type_as_string[3]) {
  case 0:
    return MatcherData::EXACT;
  case 1:
    return MatcherData::SAFE_REGEX;
  }
  ASSERT("enum failure");
  return MatcherData::EXACT;
}

std::vector<MatcherData> javaObjectArrayToMatcherData(JniHelper& jni_helper, jobjectArray array,
                                                      std::string& cluster_name_out) {
  const size_t len = jni_helper.getEnv()->GetArrayLength(array);
  std::vector<MatcherData> ret;
  if (len == 0) {
    return ret;
  }
  ASSERT((len - 1) % 3 == 0);
  if ((len - 1) % 3 != 0) {
    return ret;
  }

  JavaArrayOfByteToString(
      jni_helper, static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(array, 0)),
      &cluster_name_out);
  for (size_t i = 1; i < len; i += 3) {
    std::string name;
    std::string type_as_string;
    std::string value;
    JavaArrayOfByteToString(
        jni_helper, static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(array, i)),
        &name);
    JavaArrayOfByteToString(
        jni_helper,
        static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(array, i + 1)),
        &type_as_string);
    JavaArrayOfByteToString(
        jni_helper,
        static_cast<jbyteArray>(jni_helper.getEnv()->GetObjectArrayElement(array, i + 2)), &value);
    ret.emplace_back(MatcherData(name, StringToType(type_as_string), value));
  }
  return ret;
}

void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source,
                          Envoy::Protobuf::MessageLite* dest) {
  jbyte* bytes = jni_helper.getEnv()->GetByteArrayElements(source, /* isCopy= */ nullptr);
  jsize size = jni_helper.getEnv()->GetArrayLength(source);
  bool success = dest->ParseFromArray(bytes, size);
  RELEASE_ASSERT(success, "Failed to parse protobuf message.");
  jni_helper.getEnv()->ReleaseByteArrayElements(source, bytes, 0);
}

#define JNI_UTILITY_DEFINE_CALL_METHOD(JAVA_TYPE, JNI_TYPE)                                        \
  JNI_TYPE call##JAVA_TYPE##Method(JniHelper& jni_helper, jobject object, jmethodID method_id,     \
                                   ...) {                                                          \
    va_list args;                                                                                  \
    va_start(args, method_id);                                                                     \
    JNI_TYPE result = jni_helper.getEnv()->Call##JAVA_TYPE##MethodV(object, method_id, args);      \
    va_end(args);                                                                                  \
    Envoy::JNI::Exception::checkAndClear();                                                        \
    return result;                                                                                 \
  }

// TODO(fredyw): Delete these functions are replaced them with the ones from JniHelper

void throwException(JniHelper& jni_helper, const char* java_class_name, const char* message) {
  jclass java_class = jni_helper.getEnv()->FindClass(java_class_name);
  jint error = jni_helper.getEnv()->ThrowNew(java_class, message);
  RELEASE_ASSERT(error == JNI_OK, "Failed to throw an exception.");
  jni_helper.getEnv()->DeleteLocalRef(java_class);
}

void callVoidMethod(JniHelper& jni_helper, jobject object, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  jni_helper.getEnv()->CallVoidMethodV(object, method_id, args);
  va_end(args);
  Envoy::JNI::Exception::checkAndClear();
}

JNI_UTILITY_DEFINE_CALL_METHOD(Char, jchar)
JNI_UTILITY_DEFINE_CALL_METHOD(Short, jshort)
JNI_UTILITY_DEFINE_CALL_METHOD(Int, jint)
JNI_UTILITY_DEFINE_CALL_METHOD(Long, jlong)
JNI_UTILITY_DEFINE_CALL_METHOD(Double, jdouble)
JNI_UTILITY_DEFINE_CALL_METHOD(Boolean, jboolean)
JNI_UTILITY_DEFINE_CALL_METHOD(Object, jobject)

#define JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(JAVA_TYPE, JNI_TYPE)                                 \
  JNI_TYPE callStatic##JAVA_TYPE##Method(JniHelper& jni_helper, jclass clazz, jmethodID method_id, \
                                         ...) {                                                    \
    va_list args;                                                                                  \
    va_start(args, method_id);                                                                     \
    JNI_TYPE result = jni_helper.getEnv()->CallStatic##JAVA_TYPE##MethodV(clazz, method_id, args); \
    va_end(args);                                                                                  \
    Envoy::JNI::Exception::checkAndClear();                                                        \
    return result;                                                                                 \
  }

void callStaticVoidMethod(JniHelper& jni_helper, jclass clazz, jmethodID method_id, ...) {
  va_list args;
  va_start(args, method_id);
  jni_helper.getEnv()->CallStaticVoidMethodV(clazz, method_id, args);
  va_end(args);
  Envoy::JNI::Exception::checkAndClear();
}

JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Char, jchar)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Short, jshort)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Int, jint)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Long, jlong)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Double, jdouble)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Boolean, jboolean)
JNI_UTILITY_DEFINE_CALL_STATIC_METHOD(Object, jobject)

} // namespace JNI
} // namespace Envoy
