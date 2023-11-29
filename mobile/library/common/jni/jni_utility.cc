#include "library/common/jni/jni_utility.h"

#include <cstdlib>
#include <cstring>

#include "source/common/common/assert.h"

#include "library/common/jni/jni_support.h"
#include "library/common/jni/types/env.h"
#include "library/common/jni/types/exception.h"

namespace Envoy {
namespace JNI {

static jobject static_class_loader = nullptr;

void setClassLoader(jobject class_loader) { static_class_loader = class_loader; }

jobject getClassLoader() {
  RELEASE_ASSERT(static_class_loader,
                 "findClass() is used before calling AndroidJniLibrary.load()");
  return static_class_loader;
}

LocalRefUniquePtr<jclass> findClass(const char* class_name) {
  JniHelper jni_helper(getEnv());
  LocalRefUniquePtr<jclass> class_loader = jni_helper.findClass("java/lang/ClassLoader");
  jmethodID find_class_method = jni_helper.getMethodId(class_loader.get(), "loadClass",
                                                       "(Ljava/lang/String;)Ljava/lang/Class;");
  LocalRefUniquePtr<jstring> str_class_name = jni_helper.newStringUtf(class_name);
  LocalRefUniquePtr<jclass> clazz = jni_helper.callObjectMethod<jclass>(
      getClassLoader(), find_class_method, str_class_name.get());
  return clazz;
}

JNIEnv* getEnv() { return Envoy::JNI::Env::get(); }

void jniDeleteGlobalRef(void* context) {
  JNIEnv* env = getEnv();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

void jniDeleteConstGlobalRef(const void* context) {
  jniDeleteGlobalRef(const_cast<void*>(context));
}

int javaIntegerTotInt(JniHelper& jni_helper, jobject boxed_integer) {
  LocalRefUniquePtr<jclass> jcls_Integer = jni_helper.findClass("java/lang/Integer");
  jmethodID jmid_intValue = jni_helper.getMethodId(jcls_Integer.get(), "intValue", "()I");
  return jni_helper.callIntMethod(boxed_integer, jmid_intValue);
}

envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data) {
  size_t data_length = static_cast<size_t>(jni_helper.getArrayLength(j_data));
  return javaByteArrayToEnvoyData(jni_helper, j_data, data_length);
}

envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data, size_t data_length) {
  uint8_t* native_bytes = static_cast<uint8_t*>(safe_malloc(data_length));
  Envoy::JNI::PrimitiveArrayCriticalUniquePtr<void> critical_data =
      jni_helper.getPrimitiveArrayCritical(j_data, nullptr);
  memcpy(native_bytes, critical_data.get(), data_length); // NOLINT(safe-memcpy)
  return {data_length, native_bytes, free, native_bytes};
}

LocalRefUniquePtr<jstring> envoyDataToJavaString(JniHelper& jni_helper, envoy_data data) {
  // Ensure we get a null-terminated string, the data coming in via envoy_data might not be.
  std::string str(reinterpret_cast<const char*>(data.bytes), data.length);
  return jni_helper.newStringUtf(str.c_str());
}

LocalRefUniquePtr<jbyteArray> envoyDataToJavaByteArray(JniHelper& jni_helper, envoy_data data) {
  LocalRefUniquePtr<jbyteArray> j_data = jni_helper.newByteArray(data.length);
  PrimitiveArrayCriticalUniquePtr<void> critical_data =
      jni_helper.getPrimitiveArrayCritical(j_data.get(), nullptr);
  RELEASE_ASSERT(critical_data != nullptr, "unable to allocate memory in jni_utility");
  memcpy(critical_data.get(), data.bytes, data.length); // NOLINT(safe-memcpy)
  return j_data;
}

LocalRefUniquePtr<jlongArray> envoyStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                                              envoy_stream_intel stream_intel) {
  LocalRefUniquePtr<jlongArray> j_array = jni_helper.newLongArray(4);
  PrimitiveArrayCriticalUniquePtr<jlong> critical_array =
      jni_helper.getPrimitiveArrayCritical<jlong*>(j_array.get(), nullptr);
  RELEASE_ASSERT(critical_array != nullptr, "unable to allocate memory in jni_utility");
  critical_array.get()[0] = static_cast<jlong>(stream_intel.stream_id);
  critical_array.get()[1] = static_cast<jlong>(stream_intel.connection_id);
  critical_array.get()[2] = static_cast<jlong>(stream_intel.attempt_count);
  critical_array.get()[3] = static_cast<jlong>(stream_intel.consumed_bytes_from_response);
  return j_array;
}

LocalRefUniquePtr<jlongArray>
envoyFinalStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                     envoy_final_stream_intel final_stream_intel) {
  LocalRefUniquePtr<jlongArray> j_array = jni_helper.newLongArray(16);
  PrimitiveArrayCriticalUniquePtr<jlong> critical_array =
      jni_helper.getPrimitiveArrayCritical<jlong*>(j_array.get(), nullptr);
  RELEASE_ASSERT(critical_array != nullptr, "unable to allocate memory in jni_utility");

  critical_array.get()[0] = static_cast<jlong>(final_stream_intel.stream_start_ms);
  critical_array.get()[1] = static_cast<jlong>(final_stream_intel.dns_start_ms);
  critical_array.get()[2] = static_cast<jlong>(final_stream_intel.dns_end_ms);
  critical_array.get()[3] = static_cast<jlong>(final_stream_intel.connect_start_ms);
  critical_array.get()[4] = static_cast<jlong>(final_stream_intel.connect_end_ms);
  critical_array.get()[5] = static_cast<jlong>(final_stream_intel.ssl_start_ms);
  critical_array.get()[6] = static_cast<jlong>(final_stream_intel.ssl_end_ms);
  critical_array.get()[7] = static_cast<jlong>(final_stream_intel.sending_start_ms);
  critical_array.get()[8] = static_cast<jlong>(final_stream_intel.sending_end_ms);
  critical_array.get()[9] = static_cast<jlong>(final_stream_intel.response_start_ms);
  critical_array.get()[10] = static_cast<jlong>(final_stream_intel.stream_end_ms);
  critical_array.get()[11] = static_cast<jlong>(final_stream_intel.socket_reused);
  critical_array.get()[12] = static_cast<jlong>(final_stream_intel.sent_byte_count);
  critical_array.get()[13] = static_cast<jlong>(final_stream_intel.received_byte_count);
  critical_array.get()[14] = static_cast<jlong>(final_stream_intel.response_flags);
  critical_array.get()[15] = static_cast<jlong>(final_stream_intel.upstream_protocol);
  return j_array;
}

LocalRefUniquePtr<jobject> envoyMapToJavaMap(JniHelper& jni_helper, envoy_map map) {
  LocalRefUniquePtr<jclass> jcls_hashMap = jni_helper.findClass("java/util/HashMap");
  jmethodID jmid_hashMapInit = jni_helper.getMethodId(jcls_hashMap.get(), "<init>", "(I)V");
  LocalRefUniquePtr<jobject> j_hashMap =
      jni_helper.newObject(jcls_hashMap.get(), jmid_hashMapInit, map.length);
  jmethodID jmid_hashMapPut = jni_helper.getMethodId(
      jcls_hashMap.get(), "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    LocalRefUniquePtr<jstring> key = envoyDataToJavaString(jni_helper, map.entries[i].key);
    LocalRefUniquePtr<jstring> value = envoyDataToJavaString(jni_helper, map.entries[i].value);
    LocalRefUniquePtr<jobject> ignored =
        jni_helper.callObjectMethod(j_hashMap.get(), jmid_hashMapPut, key.get(), value.get());
  }
  return j_hashMap;
}

envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data) {
  // Returns -1 if the buffer is not a direct buffer.
  jlong data_length = jni_helper.getDirectBufferCapacity(j_data);

  if (data_length < 0) {
    LocalRefUniquePtr<jclass> jcls_ByteBuffer = jni_helper.findClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getMethodId(jcls_ByteBuffer.get(), "array", "()[B");
    LocalRefUniquePtr<jbyteArray> array =
        jni_helper.callObjectMethod<jbyteArray>(j_data, jmid_array);
    envoy_data native_data = javaByteArrayToEnvoyData(jni_helper, array.get());
    return native_data;
  }

  return javaByteBufferToEnvoyData(jni_helper, j_data, static_cast<size_t>(data_length));
}

envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data, size_t data_length) {
  // Returns nullptr if the buffer is not a direct buffer.
  uint8_t* direct_address = jni_helper.getDirectBufferAddress<uint8_t*>(j_data);

  if (direct_address == nullptr) {
    LocalRefUniquePtr<jclass> jcls_ByteBuffer = jni_helper.findClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getMethodId(jcls_ByteBuffer.get(), "array", "()[B");
    LocalRefUniquePtr<jbyteArray> array =
        jni_helper.callObjectMethod<jbyteArray>(j_data, jmid_array);
    envoy_data native_data = javaByteArrayToEnvoyData(jni_helper, array.get(), data_length);
    return native_data;
  }

  envoy_data native_data;
  native_data.bytes = direct_address;
  native_data.length = data_length;
  native_data.release = jniDeleteGlobalRef;
  native_data.context = jni_helper.newGlobalRef(j_data).release();

  return native_data;
}

envoy_data* javaByteBufferToEnvoyDataPtr(JniHelper& jni_helper, jobject j_data) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (j_data == nullptr) {
    return nullptr;
  }

  envoy_data* native_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_data = javaByteBufferToEnvoyData(jni_helper, j_data);
  return native_data;
}

envoy_headers javaArrayOfObjectArrayToEnvoyHeaders(JniHelper& jni_helper, jobjectArray headers) {
  return javaArrayOfObjectArrayToEnvoyMap(jni_helper, headers);
}

envoy_headers* javaArrayOfObjectArrayToEnvoyHeadersPtr(JniHelper& jni_helper,
                                                       jobjectArray headers) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (headers == nullptr) {
    return nullptr;
  }

  envoy_headers* native_headers = static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_headers = javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, headers);
  return native_headers;
}

envoy_stats_tags javaArrayOfObjectArrayToEnvoyStatsTags(JniHelper& jni_helper, jobjectArray tags) {
  return javaArrayOfObjectArrayToEnvoyMap(jni_helper, tags);
}

envoy_map javaArrayOfObjectArrayToEnvoyMap(JniHelper& jni_helper, jobjectArray entries) {
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_map_entry.
  envoy_map_size_t length = jni_helper.getArrayLength(entries);
  if (length == 0) {
    return {0, nullptr};
  }

  envoy_map_entry* entry_array =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * length / 2));

  for (envoy_map_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    LocalRefUniquePtr<jbyteArray> j_key = jni_helper.getObjectArrayElement<jbyteArray>(entries, i);
    envoy_data entry_key = javaByteArrayToEnvoyData(jni_helper, j_key.get());

    // Copy native byte array for header value
    LocalRefUniquePtr<jbyteArray> j_value =
        jni_helper.getObjectArrayElement<jbyteArray>(entries, i + 1);
    envoy_data entry_value = javaByteArrayToEnvoyData(jni_helper, j_value.get());

    entry_array[i / 2] = {entry_key, entry_value};
  }

  envoy_map native_map = {length / 2, entry_array};
  return native_map;
}

LocalRefUniquePtr<jobjectArray>
envoyHeadersToJavaArrayOfObjectArray(JniHelper& jni_helper,
                                     const Envoy::Types::ManagedEnvoyHeaders& map) {
  LocalRefUniquePtr<jclass> jcls_byte_array = jni_helper.findClass("java/lang/Object");
  LocalRefUniquePtr<jobjectArray> javaArray =
      jni_helper.newObjectArray(2 * map.get().length, jcls_byte_array.get(), nullptr);

  for (envoy_map_size_t i = 0; i < map.get().length; i++) {
    LocalRefUniquePtr<jbyteArray> key =
        envoyDataToJavaByteArray(jni_helper, map.get().entries[i].key);
    LocalRefUniquePtr<jbyteArray> value =
        envoyDataToJavaByteArray(jni_helper, map.get().entries[i].value);

    jni_helper.setObjectArrayElement(javaArray.get(), 2 * i, key.get());
    jni_helper.setObjectArrayElement(javaArray.get(), 2 * i + 1, value.get());
  }

  return javaArray;
}

LocalRefUniquePtr<jobjectArray>
vectorStringToJavaArrayOfByteArray(JniHelper& jni_helper, const std::vector<std::string>& v) {
  LocalRefUniquePtr<jclass> jcls_byte_array = jni_helper.findClass("[B");
  LocalRefUniquePtr<jobjectArray> joa =
      jni_helper.newObjectArray(v.size(), jcls_byte_array.get(), nullptr);

  for (size_t i = 0; i < v.size(); ++i) {
    LocalRefUniquePtr<jbyteArray> byte_array = byteArrayToJavaByteArray(
        jni_helper, reinterpret_cast<const uint8_t*>(v[i].data()), v[i].length());
    jni_helper.setObjectArrayElement(joa.get(), i, byte_array.get());
  }
  return joa;
}

LocalRefUniquePtr<jbyteArray> byteArrayToJavaByteArray(JniHelper& jni_helper, const uint8_t* bytes,
                                                       size_t len) {
  LocalRefUniquePtr<jbyteArray> byte_array = jni_helper.newByteArray(len);
  const jbyte* jbytes = reinterpret_cast<const jbyte*>(bytes);
  jni_helper.setByteArrayRegion(byte_array.get(), /*start=*/0, len, jbytes);
  return byte_array;
}

LocalRefUniquePtr<jbyteArray> stringToJavaByteArray(JniHelper& jni_helper, const std::string& str) {
  const uint8_t* str_bytes = reinterpret_cast<const uint8_t*>(str.data());
  return byteArrayToJavaByteArray(jni_helper, str_bytes, str.size());
}

void javaArrayOfByteArrayToStringVector(JniHelper& jni_helper, jobjectArray array,
                                        std::vector<std::string>* out) {
  ASSERT(out);
  ASSERT(array);
  size_t len = jni_helper.getArrayLength(array);
  out->resize(len);

  for (size_t i = 0; i < len; ++i) {
    LocalRefUniquePtr<jbyteArray> bytes_array =
        jni_helper.getObjectArrayElement<jbyteArray>(array, i);
    jsize bytes_len = jni_helper.getArrayLength(bytes_array.get());
    // It doesn't matter if the array returned by GetByteArrayElements is a copy
    // or not, as the data will be simply be copied into C++ owned memory below.
    ArrayElementsUniquePtr<jbyteArray, jbyte> bytes =
        jni_helper.getByteArrayElements(bytes_array.get(), /* is_copy= */ nullptr);
    (*out)[i].assign(reinterpret_cast<const char*>(bytes.get()), bytes_len);
  }
}

void javaByteArrayToString(JniHelper& jni_helper, jbyteArray jbytes, std::string* out) {
  std::vector<uint8_t> bytes;
  javaByteArrayToByteVector(jni_helper, jbytes, &bytes);
  *out = std::string(bytes.begin(), bytes.end());
}

void javaByteArrayToByteVector(JniHelper& jni_helper, jbyteArray array, std::vector<uint8_t>* out) {
  const size_t len = jni_helper.getArrayLength(array);
  out->resize(len);

  // It doesn't matter if the array returned by GetByteArrayElements is a copy
  // or not, as the data will be simply be copied into C++ owned memory below.
  ArrayElementsUniquePtr<jbyteArray, jbyte> jbytes =
      jni_helper.getByteArrayElements(array, /* is_copy= */ nullptr);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(jbytes.get());
  std::copy(bytes, bytes + len, out->begin());
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

void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source,
                          Envoy::Protobuf::MessageLite* dest) {
  ArrayElementsUniquePtr<jbyteArray, jbyte> bytes =
      jni_helper.getByteArrayElements(source, /* is_copy= */ nullptr);
  jsize size = jni_helper.getArrayLength(source);
  bool success = dest->ParseFromArray(bytes.get(), size);
  RELEASE_ASSERT(success, "Failed to parse protobuf message.");
}

LocalRefUniquePtr<jbyteArray> protoToJavaByteArray(JniHelper& jni_helper,
                                                   const Envoy::Protobuf::MessageLite& source) {
  size_t size = source.ByteSizeLong();
  LocalRefUniquePtr<jbyteArray> byte_array = jni_helper.newByteArray(size);
  auto bytes = jni_helper.getByteArrayElements(byte_array.get(), nullptr);
  source.SerializeToArray(bytes.get(), size);
  return byte_array;
}

std::string javaStringToString(JniHelper& jni_helper, jstring java_string) {
  if (!java_string) {
    return "";
  }
  StringUtfUniquePtr native_java_string = jni_helper.getStringUtfChars(java_string, nullptr);
  std::string cpp_string(native_java_string.get());
  return cpp_string;
}

} // namespace JNI
} // namespace Envoy
