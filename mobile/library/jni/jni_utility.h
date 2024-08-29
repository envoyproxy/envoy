#pragma once

#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "library/common/types/c_types.h"
#include "library/common/types/managed_envoy_headers.h"
#include "library/jni/jni_helper.h"

namespace Envoy {
namespace JNI {

struct JniUtility {
  /** Initializes the caches for the `JniUtility`. */
  static void initCache();
};

void jniDeleteGlobalRef(void* context);

void jniDeleteConstGlobalRef(const void* context);

/** Converts `java.lang.Integer` to C++ `int`. */
int javaIntegerToCppInt(JniHelper& jni_helper, jobject boxed_integer);

/** Converts from Java byte array to `envoy_data`. */
envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data);

/** Converts from Java byte array with the specified length to `envoy_data`. */
envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data, size_t data_length);

/** Converts from `envoy_data` to Java byte array. */
LocalRefUniquePtr<jbyteArray> envoyDataToJavaByteArray(JniHelper& jni_helper, envoy_data data);

/** Converts from `envoy_data to `java.nio.ByteBuffer`. */
LocalRefUniquePtr<jobject> envoyDataToJavaByteBuffer(JniHelper& jni_helper, envoy_data data);

/** Converts from `envoy_stream_intel` to Java long array. */
LocalRefUniquePtr<jlongArray> envoyStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                                              envoy_stream_intel stream_intel);

/** Converts from `envoy_final_stream_intel` to Java long array. */
LocalRefUniquePtr<jlongArray>
envoyFinalStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                     envoy_final_stream_intel final_stream_intel);

/** Converts from Java `ByteBuffer` to `envoy_data`. */
envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data);

/** Converts from Java `ByteBuffer` to `envoy_data` with the given length. */
envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data, jlong data_length);

/** Returns the pointer of conversion from Java `ByteBuffer` to `envoy_data`. */
envoy_data* javaByteBufferToEnvoyDataPtr(JniHelper& jni_helper, jobject j_data);

/** Converts from Java array of object array[2] (key-value pairs) to `envoy_headers`. */
envoy_headers javaArrayOfObjectArrayToEnvoyHeaders(JniHelper& jni_helper, jobjectArray headers);

/** Returns the pointer of conversion from Java array of object array[2] (key-value pairs) to
 * `envoy_headers`. */
envoy_headers* javaArrayOfObjectArrayToEnvoyHeadersPtr(JniHelper& jni_helper, jobjectArray headers);

/** Converts from Java array of object array[2] (key-value pairs) to `envoy_stats_tags`. */
envoy_stats_tags javaArrayOfObjectArrayToEnvoyStatsTags(JniHelper& jni_helper, jobjectArray tags);

/** Converts from Java array of object array[2] (key-value pairs) to `envoy_map`. */
envoy_map javaArrayOfObjectArrayToEnvoyMap(JniHelper& jni_helper, jobjectArray entries);

/** Converts from `ManagedEnvoyHeaders` to Java array of object array[2] (key-value pairs). */
LocalRefUniquePtr<jobjectArray>
envoyHeadersToJavaArrayOfObjectArray(JniHelper& jni_helper,
                                     const Envoy::Types::ManagedEnvoyHeaders& map);

/** Converts from C++ vector of strings to Java array of byte array. */
LocalRefUniquePtr<jobjectArray>
vectorStringToJavaArrayOfByteArray(JniHelper& jni_helper, const std::vector<std::string>& v);

/** Converts from C++ byte array to Java byte array. */
LocalRefUniquePtr<jbyteArray> byteArrayToJavaByteArray(JniHelper& jni_helper, const uint8_t* bytes,
                                                       size_t len);

/** Converts from C++ string to Java byte array. */
LocalRefUniquePtr<jbyteArray> stringToJavaByteArray(JniHelper& jni_helper, const std::string& str);

/** Converts from Java array of byte array to C++ vector of strings. */
void javaArrayOfByteArrayToStringVector(JniHelper& jni_helper, jobjectArray array,
                                        std::vector<std::string>* out);

/** Converts from Java byte array to C++ vector of bytes. */
void javaByteArrayToByteVector(JniHelper& jni_helper, jbyteArray array, std::vector<uint8_t>* out);

/** Converts from Java byte array to C++ string. */
void javaByteArrayToString(JniHelper& jni_helper, jbyteArray jbytes, std::string* out);

/** Parses the proto from Java byte array and stores the output into `dest` proto. */
void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source, Envoy::Protobuf::Message* dest);

/** Converts from Proto to Java byte array. */
LocalRefUniquePtr<jbyteArray> protoToJavaByteArray(JniHelper& jni_helper,
                                                   const Envoy::Protobuf::Message& source);

/** Converts from Java `String` to C++ `std::string`. */
std::string javaStringToCppString(JniHelper& jni_helper, jstring java_string);

/** Converts from C++ `std::string` to Java `String`. */
LocalRefUniquePtr<jstring> cppStringToJavaString(JniHelper& jni_helper,
                                                 const std::string& cpp_string);

/** Converts from C++'s map-type<std::string, std::string> to Java `HashMap<String, String>`. */
template <typename MapType>
LocalRefUniquePtr<jobject> cppMapToJavaMap(JniHelper& jni_helper, const MapType& cpp_map) {
  jclass java_map_class = jni_helper.findClassFromCache("java/util/HashMap");
  auto java_map_init_method_id = jni_helper.getMethodIdFromCache(java_map_class, "<init>", "(I)V");
  auto java_map_put_method_id = jni_helper.getMethodIdFromCache(
      java_map_class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  auto java_map_object =
      jni_helper.newObject(java_map_class, java_map_init_method_id, cpp_map.size());
  for (const auto& [cpp_key, cpp_value] : cpp_map) {
    auto java_key = cppStringToJavaString(jni_helper, cpp_key);
    auto java_value = cppStringToJavaString(jni_helper, cpp_value);
    auto ignored = jni_helper.callObjectMethod(java_map_object.get(), java_map_put_method_id,
                                               java_key.get(), java_value.get());
  }
  return java_map_object;
}

/**
 * Converts from Java's `Map<String, String>` to C++'s `absl::flat_hash_map<std::string,
 * std::string>`.
 */
absl::flat_hash_map<std::string, std::string> javaMapToCppMap(JniHelper& jni_helper,
                                                              jobject java_map);

/**
 * Converts from C++ `HeaderMap` to Java `Map<String, List<String>>`.
 *
 * Both `RequestHeaderMap` and `RequestTrailerMap` inherit from `HeaderMap`. So this function can be
 * used for converting trailers, too.
 */
LocalRefUniquePtr<jobject> cppHeadersToJavaHeaders(JniHelper& jni_helper,
                                                   const Http::HeaderMap& cpp_headers);

/**
 * Converts from Java `Map<String, List<String>>` to C++ `HeaderMap`.
 *
 * Both `RequestHeaderMap` and `RequestTrailerMap` inherit from `HeaderMap`. So this function can be
 * used for converting trailers, too.
 */
void javaHeadersToCppHeaders(JniHelper& jni_helper, jobject java_headers,
                             Http::HeaderMap& cpp_headers);

/**
 * Returns true if the specified `java_byte_buffer` is a direct ByteBuffer; false otherwise.
 */
bool isJavaDirectByteBuffer(JniHelper& jni_helper, jobject java_byte_buffer);

/**
 * Converts from Java direct `ByteBuffer` (off the JVM heap) to `Envoy::Buffer::Instance` up to the
 * specified length.
 *
 * The function will avoid copying the data from the Java `ByteBuffer` into
 * `Envoy::Buffer::Instance`.
 */
Buffer::InstancePtr javaDirectByteBufferToCppBufferInstance(JniHelper& jni_helper,
                                                            jobject java_byte_buffer, jlong length);

/**
 * Converts from `Envoy::Buffer::Instance` to Java direct `ByteBuffer` (off the JVM heap) up to the
 * specified length.
 *
 * The function will avoid copying the data from `Envoy::Buffer::Instance` into the `ByteBuffer`.
 */
LocalRefUniquePtr<jobject> cppBufferInstanceToJavaDirectByteBuffer(
    JniHelper& jni_helper, const Buffer::Instance& cpp_buffer_instance, uint64_t length);

/**
 * Converts from Java non-direct `ByteBuffer` (on the JVM heap) to `Envoy::Buffer::Instance` up
 * to the specified length.
 *
 * The function will copy the data from the Java `ByteBuffer` into `Envoy::Buffer::Instance`.
 */
Buffer::InstancePtr javaNonDirectByteBufferToCppBufferInstance(JniHelper& jni_helper,
                                                               jobject java_byte_buffer,
                                                               jlong length);

/**
 * Converts from `Envoy::Buffer::Instance` to Java non-direct `ByteBuffer` (off the JVM heap) up to
 * the specified length.
 *
 * The function will copy the data from `Envoy::Buffer::Instance` into the `ByteBuffer`.
 */
LocalRefUniquePtr<jobject> cppBufferInstanceToJavaNonDirectByteBuffer(
    JniHelper& jni_helper, const Buffer::Instance& cpp_buffer_instance, uint64_t length);

/** Gets the Java exception message from the `throwable`. */
std::string getJavaExceptionMessage(JniHelper& jni_helper, jthrowable throwable);

/**
 * Converts from Java `io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel` to C++
 * `envoy_stream_intel`.
 */
envoy_stream_intel javaStreamIntelToCppStreamIntel(JniHelper& jni_helper,
                                                   jobject java_stream_intel);

/**
 * Converts from C++ `envoy_stream_intel` to Java  to
 * `io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel`.
 */
LocalRefUniquePtr<jobject> cppStreamIntelToJavaStreamIntel(JniHelper& jni_helper,
                                                           const envoy_stream_intel& stream_intel);

/**
 * Converts from Java `io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel` to C++
 * `envoy_final_stream_intel`.
 */
envoy_final_stream_intel javaFinalStreamIntelToCppFinalStreamIntel(JniHelper& jni_helper,
                                                                   jobject java_final_stream_intel);

/**
 * Converts from C++ `envoy_final_stream_intel` to Java  to
 * `io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel`.
 */
LocalRefUniquePtr<jobject>
cppFinalStreamIntelToJavaFinalStreamIntel(JniHelper& jni_helper,
                                          const envoy_final_stream_intel& final_stream_intel);

} // namespace JNI
} // namespace Envoy
