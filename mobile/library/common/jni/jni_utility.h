#pragma once

#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_helper.h"
#include "library/common/types/c_types.h"
#include "library/common/types/managed_envoy_headers.h"
#include "library/common/types/matcher_data.h"

namespace Envoy {
namespace JNI {

// TODO(Augustyniak): Replace the usages of this global method with Envoy::JNI::Env::get()
JNIEnv* getEnv();

void setClassLoader(jobject class_loader);

/**
 * Finds a class with a given name using a class loader provided with the use
 * of `setClassLoader` function. The class loader is supposed to come from
 * application's context and should be associated with project's code - Java classes
 * defined by the project. For finding classes of Java built in-types use
 * `env->FindClass(...)` method instead as it is lighter to use.
 *
 * Read more about why you cannot use `env->FindClass(...)` to look for Java classes
 * defined by the project and a pattern used by the implementation of `findClass` helper
 * method at https://developer.android.com/training/articles/perf-jni#native-libraries.
 *
 * The method works on Android targets only as the `setClassLoader` method is not
 * called by JVM-only targets.
 *
 * @param class_name, the name of the class to find (i.e.
 * "io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary").
 *
 * @return jclass, the class with a provided `class_name` or NULL if
 *         it couldn't be found.
 */
LocalRefUniquePtr<jclass> findClass(const char* class_name);

void jniDeleteGlobalRef(void* context);

void jniDeleteConstGlobalRef(const void* context);

/** Converts `java.lang.Integer` to C++ `int`. */
int javaIntegerTotInt(JniHelper& jni_helper, jobject boxed_integer);

/** Converts from Java byte array to `envoy_data`. */
envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data);

/** Converts from Java byte array with the specified length to `envoy_data`. */
envoy_data javaByteArrayToEnvoyData(JniHelper& jni_helper, jbyteArray j_data, size_t data_length);

/** Converts from `envoy_data` to Java byte array. */
LocalRefUniquePtr<jbyteArray> envoyDataToJavaByteArray(JniHelper& jni_helper, envoy_data data);

/** Converts from `envoy_stream_intel` to Java long array. */
LocalRefUniquePtr<jlongArray> envoyStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                                              envoy_stream_intel stream_intel);

/** Converts from `envoy_final_stream_intel` to Java long array. */
LocalRefUniquePtr<jlongArray>
envoyFinalStreamIntelToJavaLongArray(JniHelper& jni_helper,
                                     envoy_final_stream_intel final_stream_intel);

/** Converts from Java `Map` to `envoy_map`. */
LocalRefUniquePtr<jobject> envoyMapToJavaMap(JniHelper& jni_helper, envoy_map map);

/** Converts from `envoy_data` to Java `String`. */
LocalRefUniquePtr<jstring> envoyDataToJavaString(JniHelper& jni_helper, envoy_data data);

/** Converts from Java `ByteBuffer` to `envoy_data`. */
envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data);

/** Converts from Java `ByteBuffer` to `envoy_data` with the given length. */
envoy_data javaByteBufferToEnvoyData(JniHelper& jni_helper, jobject j_data, size_t data_length);

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
void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source,
                          Envoy::Protobuf::MessageLite* dest);

/** Converts from Proto to Java byte array. */
LocalRefUniquePtr<jbyteArray> protoToJavaByteArray(JniHelper& jni_helper,
                                                   const Envoy::Protobuf::MessageLite& source);

/** Converts from Java `String` to C++ string. */
std::string javaStringToString(JniHelper& jni_helper, jstring java_string);

} // namespace JNI
} // namespace Envoy
