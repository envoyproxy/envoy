#include "library/jni/jni_utility.h"

#include <cstdlib>
#include <cstring>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"

#include "library/common/types/matcher_data.h"

namespace Envoy {
namespace JNI {

void JniUtility::initCache() {
  Envoy::JNI::JniHelper::addToCache("java/lang/Object", /* methods= */ {},
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/lang/Integer",
                                    /* methods= */
                                    {
                                        {"<init>", "(I)V"},
                                        {"intValue", "()I"},
                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/lang/ClassLoader", /* methods= */ {},
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/nio/ByteBuffer",
                                    /* methods= */
                                    {
                                        {"array", "()[B"},
                                        {"isDirect", "()Z"},
                                    },
                                    /* static_methods = */
                                    {
                                        {"wrap", "([B)Ljava/nio/ByteBuffer;"},
                                    },
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/lang/Throwable",
                                    /* methods= */
                                    {
                                        {"getMessage", "()Ljava/lang/String;"},
                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/lang/UnsupportedOperationException",
                                    /* methods= */ {}, /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("[B", /* methods= */ {}, /* static_methods = */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "java/util/LinkedHashMap", /* methods= */
      {
          {"<init>", "()V"},
          {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
          {"get", "(Ljava/lang/Object;)Ljava/lang/Object;"},
      },
      /* static_methods = */ {}, /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Map", /* methods= */
                                    {
                                        {"entrySet", "()Ljava/util/Set;"},

                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Map$Entry", /* methods= */
                                    {
                                        {"getKey", "()Ljava/lang/Object;"},
                                        {"getValue", "()Ljava/lang/Object;"},

                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Set", /* methods= */
                                    {
                                        {"iterator", "()Ljava/util/Iterator;"},

                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("java/util/Iterator", /* methods= */
                                    {{"hasNext", "()Z"}, {"next", "()Ljava/lang/Object;"}

                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "java/util/HashMap", /* methods= */
      {{"<init>", "(I)V"}, {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"}},
      /* static_methods = */ {}, /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "java/util/List", /* methods= */ {{"size", "()I"}, {"get", "(I)Ljava/lang/Object;"}},
      /* static_methods = */ {}, /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "java/util/ArrayList", /* methods= */ {{"<init>", "()V"}, {"add", "(Ljava/lang/Object;)Z"}},
      /* static_methods = */ {}, /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel",
                                    /* methods= */
                                    {
                                        {"<init>", "(JJJJ)V"},
                                        {"getStreamId", "()J"},
                                        {"getConnectionId", "()J"},
                                        {"getAttemptCount", "()J"},
                                        {"getConsumedBytesFromResponse", "()J"},
                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel",
                                    /* methods= */
                                    {
                                        {"<init>", "(JJJJJJJJJJJZJJJJ)V"},
                                        {"getStreamStartMs", "()J"},
                                        {"getDnsStartMs", "()J"},
                                        {"getDnsEndMs", "()J"},
                                        {"getConnectStartMs", "()J"},
                                        {"getConnectEndMs", "()J"},
                                        {"getSslStartMs", "()J"},
                                        {"getSslEndMs", "()J"},
                                        {"getSendingStartMs", "()J"},
                                        {"getSendingEndMs", "()J"},
                                        {"getResponseStartMs", "()J"},
                                        {"getStreamEndMs", "()J"},
                                        {"getSocketReused", "()Z"},
                                        {"getSentByteCount", "()J"},
                                        {"getReceivedByteCount", "()J"},
                                        {"getResponseFlags", "()J"},
                                        {"getUpstreamProtocol", "()J"},
                                    },
                                    /* static_methods = */ {}, /* fields= */ {},
                                    /* static_fields= */ {});
}

void jniDeleteGlobalRef(void* context) {
  JNIEnv* env = JniHelper::getThreadLocalEnv();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

void jniDeleteConstGlobalRef(const void* context) {
  jniDeleteGlobalRef(const_cast<void*>(context));
}

int javaIntegerToCppInt(JniHelper& jni_helper, jobject boxed_integer) {
  jclass jcls_Integer = jni_helper.findClassFromCache("java/lang/Integer");
  jmethodID jmid_intValue = jni_helper.getMethodIdFromCache(jcls_Integer, "intValue", "()I");
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

LocalRefUniquePtr<jbyteArray> envoyDataToJavaByteArray(JniHelper& jni_helper, envoy_data data) {
  LocalRefUniquePtr<jbyteArray> j_data = jni_helper.newByteArray(data.length);
  PrimitiveArrayCriticalUniquePtr<void> critical_data =
      jni_helper.getPrimitiveArrayCritical(j_data.get(), nullptr);
  RELEASE_ASSERT(critical_data != nullptr, "unable to allocate memory in jni_utility");
  memcpy(critical_data.get(), data.bytes, data.length); // NOLINT(safe-memcpy)
  return j_data;
}

LocalRefUniquePtr<jobject> envoyDataToJavaByteBuffer(JniHelper& jni_helper, envoy_data data) {
  return jni_helper.newDirectByteBuffer(
      const_cast<void*>(reinterpret_cast<const void*>(data.bytes)), data.length);
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

envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data) {
  // Returns -1 if the buffer is not a direct buffer.
  jlong data_length = jni_helper.getDirectBufferCapacity(j_data);

  if (data_length < 0) {
    jclass jcls_ByteBuffer = jni_helper.findClassFromCache("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getMethodIdFromCache(jcls_ByteBuffer, "array", "()[B");
    LocalRefUniquePtr<jbyteArray> array =
        jni_helper.callObjectMethod<jbyteArray>(j_data, jmid_array);
    envoy_data native_data = javaByteArrayToEnvoyData(jni_helper, array.get());
    return native_data;
  }

  return javaByteBufferToEnvoyData(jni_helper, j_data, data_length);
}

envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data, jlong data_length) {
  // Returns nullptr if the buffer is not a direct buffer.
  uint8_t* direct_address = jni_helper.getDirectBufferAddress<uint8_t*>(j_data);

  if (direct_address == nullptr) {
    jclass jcls_ByteBuffer = jni_helper.findClassFromCache("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = jni_helper.getMethodIdFromCache(jcls_ByteBuffer, "array", "()[B");
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
  jclass jcls_byte_array = jni_helper.findClassFromCache("java/lang/Object");
  LocalRefUniquePtr<jobjectArray> javaArray =
      jni_helper.newObjectArray(2 * map.get().length, jcls_byte_array, nullptr);

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
  jclass jcls_byte_array = jni_helper.findClassFromCache("[B");
  LocalRefUniquePtr<jobjectArray> joa =
      jni_helper.newObjectArray(v.size(), jcls_byte_array, nullptr);

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

void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source, Protobuf::Message* dest) {
  ArrayElementsUniquePtr<jbyteArray, jbyte> bytes =
      jni_helper.getByteArrayElements(source, /* is_copy= */ nullptr);
  jsize size = jni_helper.getArrayLength(source);
  bool success = dest->ParseFromArray(bytes.get(), size);
  RELEASE_ASSERT(success, "Failed to parse protobuf message.");
}

LocalRefUniquePtr<jbyteArray> protoToJavaByteArray(JniHelper& jni_helper,
                                                   const Envoy::Protobuf::Message& source) {
  size_t size = source.ByteSizeLong();
  LocalRefUniquePtr<jbyteArray> byte_array = jni_helper.newByteArray(size);
  auto bytes = jni_helper.getByteArrayElements(byte_array.get(), nullptr);
  source.SerializeToArray(bytes.get(), size);
  return byte_array;
}

std::string javaStringToCppString(JniHelper& jni_helper, jstring java_string) {
  if (!java_string) {
    return "";
  }
  StringUtfUniquePtr cpp_java_string = jni_helper.getStringUtfChars(java_string, nullptr);
  return std::string(cpp_java_string.get());
}

LocalRefUniquePtr<jstring> cppStringToJavaString(JniHelper& jni_helper,
                                                 const std::string& cpp_string) {
  return jni_helper.newStringUtf(cpp_string.c_str());
}

absl::flat_hash_map<std::string, std::string> javaMapToCppMap(JniHelper& jni_helper,
                                                              jobject java_map) {
  absl::flat_hash_map<std::string, std::string> cpp_map;

  auto java_map_class = jni_helper.findClassFromCache("java/util/Map");
  auto java_entry_set_method_id =
      jni_helper.getMethodIdFromCache(java_map_class, "entrySet", "()Ljava/util/Set;");
  auto java_entry_set_object = jni_helper.callObjectMethod(java_map, java_entry_set_method_id);

  auto java_set_class = jni_helper.findClassFromCache("java/util/Set");
  jclass java_map_entry_class = jni_helper.findClassFromCache("java/util/Map$Entry");

  auto java_iterator_method_id =
      jni_helper.getMethodIdFromCache(java_set_class, "iterator", "()Ljava/util/Iterator;");
  auto java_get_key_method_id =
      jni_helper.getMethodIdFromCache(java_map_entry_class, "getKey", "()Ljava/lang/Object;");
  auto java_get_value_method_id =
      jni_helper.getMethodIdFromCache(java_map_entry_class, "getValue", "()Ljava/lang/Object;");

  auto java_iterator_object =
      jni_helper.callObjectMethod(java_entry_set_object.get(), java_iterator_method_id);
  auto java_iterator_class = jni_helper.findClassFromCache("java/util/Iterator");
  auto java_has_next_method_id =
      jni_helper.getMethodIdFromCache(java_iterator_class, "hasNext", "()Z");
  auto java_next_method_id =
      jni_helper.getMethodIdFromCache(java_iterator_class, "next", "()Ljava/lang/Object;");

  while (jni_helper.callBooleanMethod(java_iterator_object.get(), java_has_next_method_id)) {
    auto java_entry_object =
        jni_helper.callObjectMethod(java_iterator_object.get(), java_next_method_id);
    auto java_key_object =
        jni_helper.callObjectMethod<jstring>(java_entry_object.get(), java_get_key_method_id);
    auto java_value_object =
        jni_helper.callObjectMethod<jstring>(java_entry_object.get(), java_get_value_method_id);

    std::string cpp_key = javaStringToCppString(jni_helper, java_key_object.get());
    std::string cpp_value = javaStringToCppString(jni_helper, java_value_object.get());
    cpp_map.emplace(std::move(cpp_key), std::move(cpp_value));
  }

  return cpp_map;
}

LocalRefUniquePtr<jobject> cppHeadersToJavaHeaders(JniHelper& jni_helper,
                                                   const Http::HeaderMap& cpp_headers) {
  // Use LinkedHashMap to preserve the insertion order.
  jclass java_map_class = jni_helper.findClassFromCache("java/util/LinkedHashMap");
  auto java_map_init_method_id = jni_helper.getMethodIdFromCache(java_map_class, "<init>", "()V");
  auto java_map_put_method_id = jni_helper.getMethodIdFromCache(
      java_map_class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  auto java_map_get_method_id = jni_helper.getMethodIdFromCache(
      java_map_class, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
  auto java_map_object = jni_helper.newObject(java_map_class, java_map_init_method_id);

  jclass java_list_class = jni_helper.findClassFromCache("java/util/ArrayList");
  auto java_list_init_method_id = jni_helper.getMethodIdFromCache(java_list_class, "<init>", "()V");
  auto java_list_add_method_id =
      jni_helper.getMethodIdFromCache(java_list_class, "add", "(Ljava/lang/Object;)Z");

  cpp_headers.iterate([&](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    std::string cpp_key = std::string(header.key().getStringView());
    if (cpp_headers.formatter().has_value()) {
      const Envoy::Http::StatefulHeaderKeyFormatter& formatter = cpp_headers.formatter().value();
      cpp_key = formatter.format(cpp_key);
    }
    std::string cpp_value = std::string(header.value().getStringView());

    auto java_key = cppStringToJavaString(jni_helper, cpp_key);
    auto java_value = cppStringToJavaString(jni_helper, cpp_value);

    auto existing_value =
        jni_helper.callObjectMethod(java_map_object.get(), java_map_get_method_id, java_key.get());
    if (existing_value == nullptr) { // the key does not exist
      // Create a new list.
      auto java_list_object = jni_helper.newObject(java_list_class, java_list_init_method_id);
      jni_helper.callBooleanMethod(java_list_object.get(), java_list_add_method_id,
                                   java_value.get());
      // Put the new list into the map.
      auto ignored = jni_helper.callObjectMethod(java_map_object.get(), java_map_put_method_id,
                                                 java_key.get(), java_list_object.get());
    } else {
      // Update the existing list.
      jni_helper.callBooleanMethod(existing_value.get(), java_list_add_method_id, java_value.get());
    }

    return Http::HeaderMap::Iterate::Continue;
  });

  return java_map_object;
}

void javaHeadersToCppHeaders(JniHelper& jni_helper, jobject java_headers,
                             Http::HeaderMap& cpp_headers) {
  auto java_map_class = jni_helper.findClassFromCache("java/util/Map");
  auto java_entry_set_method_id =
      jni_helper.getMethodIdFromCache(java_map_class, "entrySet", "()Ljava/util/Set;");
  auto java_entry_set_object = jni_helper.callObjectMethod(java_headers, java_entry_set_method_id);

  auto java_set_class = jni_helper.findClassFromCache("java/util/Set");
  jclass java_map_entry_class = jni_helper.findClassFromCache("java/util/Map$Entry");

  auto java_map_iter_method_id =
      jni_helper.getMethodIdFromCache(java_set_class, "iterator", "()Ljava/util/Iterator;");
  auto java_map_get_key_method_id =
      jni_helper.getMethodIdFromCache(java_map_entry_class, "getKey", "()Ljava/lang/Object;");
  auto java_map_get_value_method_id =
      jni_helper.getMethodIdFromCache(java_map_entry_class, "getValue", "()Ljava/lang/Object;");

  auto java_iter_object =
      jni_helper.callObjectMethod(java_entry_set_object.get(), java_map_iter_method_id);
  auto java_iterator_class = jni_helper.findClassFromCache("java/util/Iterator");
  auto java_iter_has_next_method_id =
      jni_helper.getMethodIdFromCache(java_iterator_class, "hasNext", "()Z");
  auto java_iter_next_method_id =
      jni_helper.getMethodIdFromCache(java_iterator_class, "next", "()Ljava/lang/Object;");

  jclass java_list_class = jni_helper.findClassFromCache("java/util/List");
  auto java_list_size_method_id = jni_helper.getMethodIdFromCache(java_list_class, "size", "()I");
  auto java_list_get_method_id =
      jni_helper.getMethodIdFromCache(java_list_class, "get", "(I)Ljava/lang/Object;");

  while (jni_helper.callBooleanMethod(java_iter_object.get(), java_iter_has_next_method_id)) {
    auto java_entry_object =
        jni_helper.callObjectMethod(java_iter_object.get(), java_iter_next_method_id);
    auto java_key_object =
        jni_helper.callObjectMethod<jstring>(java_entry_object.get(), java_map_get_key_method_id);
    auto java_value_object =
        jni_helper.callObjectMethod<jstring>(java_entry_object.get(), java_map_get_value_method_id);

    std::string cpp_key = javaStringToCppString(jni_helper, java_key_object.get());
    jint java_list_size =
        jni_helper.callIntMethod(java_value_object.get(), java_list_size_method_id);
    for (jint i = 0; i < java_list_size; ++i) {
      auto java_value =
          jni_helper.callObjectMethod<jstring>(java_value_object.get(), java_list_get_method_id, i);
      auto cpp_value = javaStringToCppString(jni_helper, java_value.get());
      if (cpp_headers.formatter().has_value()) {
        Http::StatefulHeaderKeyFormatter& formatter = cpp_headers.formatter().value();
        formatter.processKey(cpp_key);
      }
      cpp_headers.addCopy(Http::LowerCaseString(cpp_key), cpp_value);
    }
  }
}

bool isJavaDirectByteBuffer(JniHelper& jni_helper, jobject java_byte_buffer) {
  jclass java_byte_buffer_class = jni_helper.findClassFromCache("java/nio/ByteBuffer");
  auto java_byte_buffer_is_direct_method_id =
      jni_helper.getMethodIdFromCache(java_byte_buffer_class, "isDirect", "()Z");
  return jni_helper.callBooleanMethod(java_byte_buffer, java_byte_buffer_is_direct_method_id);
}

Buffer::InstancePtr javaDirectByteBufferToCppBufferInstance(JniHelper& jni_helper,
                                                            jobject java_byte_buffer,
                                                            jlong length) {
  ASSERT(java_byte_buffer != nullptr, "The ByteBuffer argument is not a direct ByteBuffer.");
  // Because the direct ByteBuffer is allocated in the JVM, we need to tell the JVM to not garbage
  // collect it by wrapping with a GlobalRef.
  auto java_byte_buffer_global_ref = jni_helper.newGlobalRef(java_byte_buffer).release();
  void* java_byte_buffer_address = jni_helper.getDirectBufferAddress(java_byte_buffer_global_ref);
  Buffer::BufferFragmentImpl* byte_buffer_fragment = new Buffer::BufferFragmentImpl(
      java_byte_buffer_address, static_cast<size_t>(length),
      [java_byte_buffer_global_ref](const void*, size_t,
                                    const Buffer::BufferFragmentImpl* this_fragment) {
        JniHelper::getThreadLocalEnv()->DeleteGlobalRef(java_byte_buffer_global_ref);
        delete this_fragment;
      });
  Buffer::InstancePtr cpp_buffer_instance = std::make_unique<Buffer::OwnedImpl>();
  cpp_buffer_instance->addBufferFragment(*byte_buffer_fragment);
  return cpp_buffer_instance;
}

LocalRefUniquePtr<jobject> cppBufferInstanceToJavaDirectByteBuffer(
    JniHelper& jni_helper, const Buffer::Instance& cpp_buffer_instance, uint64_t length) {
  void* data =
      const_cast<Buffer::Instance&>(cpp_buffer_instance).linearize(static_cast<uint32_t>(length));
  LocalRefUniquePtr<jobject> java_byte_buffer =
      jni_helper.newDirectByteBuffer(data, static_cast<jlong>(length));
  return java_byte_buffer;
}

Buffer::InstancePtr javaNonDirectByteBufferToCppBufferInstance(JniHelper& jni_helper,
                                                               jobject java_byte_buffer,
                                                               jlong length) {
  jclass java_byte_buffer_class = jni_helper.findClassFromCache("java/nio/ByteBuffer");
  auto java_byte_buffer_array_method_id =
      jni_helper.getMethodIdFromCache(java_byte_buffer_class, "array", "()[B");
  auto java_byte_array =
      jni_helper.callObjectMethod<jbyteArray>(java_byte_buffer, java_byte_buffer_array_method_id);
  ASSERT(java_byte_array != nullptr, "The ByteBuffer argument is not a non-direct ByteBuffer.");
  auto java_byte_array_elements = jni_helper.getByteArrayElements(java_byte_array.get(), nullptr);
  Buffer::InstancePtr cpp_buffer_instance = std::make_unique<Buffer::OwnedImpl>();
  cpp_buffer_instance->add(static_cast<void*>(java_byte_array_elements.get()),
                           static_cast<uint64_t>(length));
  return cpp_buffer_instance;
}

LocalRefUniquePtr<jobject> cppBufferInstanceToJavaNonDirectByteBuffer(
    JniHelper& jni_helper, const Buffer::Instance& cpp_buffer_instance, uint64_t length) {
  jclass java_byte_buffer_class = jni_helper.findClassFromCache("java/nio/ByteBuffer");
  auto java_byte_buffer_wrap_method_id = jni_helper.getStaticMethodIdFromCache(
      java_byte_buffer_class, "wrap", "([B)Ljava/nio/ByteBuffer;");
  auto java_byte_array = jni_helper.newByteArray(static_cast<jsize>(cpp_buffer_instance.length()));
  auto java_byte_array_elements = jni_helper.getByteArrayElements(java_byte_array.get(), nullptr);
  cpp_buffer_instance.copyOut(0, length, static_cast<void*>(java_byte_array_elements.get()));
  return jni_helper.callStaticObjectMethod(java_byte_buffer_class, java_byte_buffer_wrap_method_id,
                                           java_byte_array.get());
}

std::string getJavaExceptionMessage(JniHelper& jni_helper, jthrowable throwable) {
  jclass java_throwable_class = jni_helper.findClassFromCache("java/lang/Throwable");
  auto java_get_message_method_id =
      jni_helper.getMethodIdFromCache(java_throwable_class, "getMessage", "()Ljava/lang/String;");
  auto java_exception_message =
      jni_helper.callObjectMethod<jstring>(throwable, java_get_message_method_id);
  return javaStringToCppString(jni_helper, java_exception_message.get());
}

envoy_stream_intel javaStreamIntelToCppStreamIntel(JniHelper& jni_helper,
                                                   jobject java_stream_intel) {
  auto java_stream_intel_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel");
  jlong java_stream_id = jni_helper.callLongMethod(
      java_stream_intel,
      jni_helper.getMethodIdFromCache(java_stream_intel_class, "getStreamId", "()J"));
  jlong java_connection_id = jni_helper.callLongMethod(
      java_stream_intel,
      jni_helper.getMethodIdFromCache(java_stream_intel_class, "getConnectionId", "()J"));
  jlong java_attempt_count = jni_helper.callLongMethod(
      java_stream_intel,
      jni_helper.getMethodIdFromCache(java_stream_intel_class, "getAttemptCount", "()J"));
  jlong java_consumed_bytes_from_response = jni_helper.callLongMethod(
      java_stream_intel, jni_helper.getMethodIdFromCache(java_stream_intel_class,
                                                         "getConsumedBytesFromResponse", "()J"));

  return {
      /* stream_id= */ static_cast<int64_t>(java_stream_id),
      /* connection_id= */ static_cast<int64_t>(java_connection_id),
      /* attempt_count= */ static_cast<uint64_t>(java_attempt_count),
      /* consumed_bytes_from_response= */ static_cast<uint64_t>(java_consumed_bytes_from_response),
  };
}

LocalRefUniquePtr<jobject> cppStreamIntelToJavaStreamIntel(JniHelper& jni_helper,
                                                           const envoy_stream_intel& stream_intel) {
  auto java_stream_intel_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel");
  auto java_stream_intel_init_method_id =
      jni_helper.getMethodIdFromCache(java_stream_intel_class, "<init>", "(JJJJ)V");
  return jni_helper.newObject(java_stream_intel_class, java_stream_intel_init_method_id,
                              static_cast<jlong>(stream_intel.stream_id),
                              static_cast<jlong>(stream_intel.connection_id),
                              static_cast<jlong>(stream_intel.attempt_count),
                              static_cast<jlong>(stream_intel.consumed_bytes_from_response));
}

envoy_final_stream_intel
javaFinalStreamIntelToCppFinalStreamIntel(JniHelper& jni_helper, jobject java_final_stream_intel) {
  auto java_final_stream_intel_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel");
  jlong java_stream_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getStreamStartMs", "()J"));
  jlong java_dns_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getDnsStartMs", "()J"));
  jlong java_dns_end_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getDnsEndMs", "()J"));
  jlong java_connect_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getConnectStartMs", "()J"));
  jlong java_connect_end_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getConnectEndMs", "()J"));
  jlong java_ssl_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSslStartMs", "()J"));
  jlong java_ssl_end_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSslEndMs", "()J"));
  jlong java_sending_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSendingStartMs", "()J"));
  jlong java_sending_end_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSendingEndMs", "()J"));
  jlong java_response_start_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getResponseStartMs", "()J"));
  jlong java_stream_end_ms = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getStreamEndMs", "()J"));
  jboolean java_socket_reused = jni_helper.callBooleanMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSocketReused", "()Z"));
  jlong java_sent_byte_count = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getSentByteCount", "()J"));
  jlong java_received_byte_count = jni_helper.callLongMethod(
      java_final_stream_intel, jni_helper.getMethodIdFromCache(java_final_stream_intel_class,
                                                               "getReceivedByteCount", "()J"));
  jlong java_response_flags = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getResponseFlags", "()J"));
  jlong java_upstream_protocol = jni_helper.callLongMethod(
      java_final_stream_intel,
      jni_helper.getMethodIdFromCache(java_final_stream_intel_class, "getUpstreamProtocol", "()J"));

  return {
      /* stream_start_ms= */ static_cast<int64_t>(java_stream_start_ms),
      /* dns_start_ms= */ static_cast<int64_t>(java_dns_start_ms),
      /* dns_end_ms= */ static_cast<int64_t>(java_dns_end_ms),
      /* connect_start_ms= */ static_cast<int64_t>(java_connect_start_ms),
      /* connect_end_ms= */ static_cast<int64_t>(java_connect_end_ms),
      /* ssl_start_ms= */ static_cast<int64_t>(java_ssl_start_ms),
      /* ssl_end_ms= */ static_cast<int64_t>(java_ssl_end_ms),
      /* sending_start_ms= */ static_cast<int64_t>(java_sending_start_ms),
      /* sending_end_ms= */ static_cast<int64_t>(java_sending_end_ms),
      /* response_start_ms= */ static_cast<int64_t>(java_response_start_ms),
      /* stream_end_ms= */ static_cast<int64_t>(java_stream_end_ms),
      /* socket_reused= */ static_cast<uint64_t>((java_socket_reused == JNI_TRUE) ? 1 : 0),
      /* sent_byte_count= */ static_cast<uint64_t>(java_sent_byte_count),
      /* received_byte_count= */ static_cast<uint64_t>(java_received_byte_count),
      /* response_flags= */ static_cast<uint64_t>(java_response_flags),
      /* upstream_protocol= */ static_cast<int64_t>(java_upstream_protocol),
  };
}

LocalRefUniquePtr<jobject>
cppFinalStreamIntelToJavaFinalStreamIntel(JniHelper& jni_helper,
                                          const envoy_final_stream_intel& final_stream_intel) {
  auto java_final_stream_intel_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel");
  auto java_final_stream_intel_init_method_id = jni_helper.getMethodIdFromCache(
      java_final_stream_intel_class, "<init>", "(JJJJJJJJJJJZJJJJ)V");
  return jni_helper.newObject(java_final_stream_intel_class, java_final_stream_intel_init_method_id,
                              static_cast<jlong>(final_stream_intel.stream_start_ms),
                              static_cast<jlong>(final_stream_intel.dns_start_ms),
                              static_cast<jlong>(final_stream_intel.dns_end_ms),
                              static_cast<jlong>(final_stream_intel.connect_start_ms),
                              static_cast<jlong>(final_stream_intel.connect_end_ms),
                              static_cast<jlong>(final_stream_intel.ssl_start_ms),
                              static_cast<jlong>(final_stream_intel.ssl_end_ms),
                              static_cast<jlong>(final_stream_intel.sending_start_ms),
                              static_cast<jlong>(final_stream_intel.sending_end_ms),
                              static_cast<jlong>(final_stream_intel.response_start_ms),
                              static_cast<jlong>(final_stream_intel.stream_end_ms),
                              static_cast<jboolean>(final_stream_intel.socket_reused),
                              static_cast<jlong>(final_stream_intel.sent_byte_count),
                              static_cast<jlong>(final_stream_intel.received_byte_count),
                              static_cast<jlong>(final_stream_intel.response_flags),
                              static_cast<jlong>(final_stream_intel.upstream_protocol));
}

} // namespace JNI
} // namespace Envoy
