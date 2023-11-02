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
JNIEnv* get_env();

void set_class_loader(jobject class_loader);

/**
 * Finds a class with a given name using a class loader provided with the use
 * of `set_class_loader` function. The class loader is supposed to come from
 * application's context and should be associated with project's code - Java classes
 * defined by the project. For finding classes of Java built in-types use
 * `env->FindClass(...)` method instead as it is lighter to use.
 *
 * Read more about why you cannot use `env->FindClass(...)` to look for Java classes
 * defined by the project and a pattern used by the implementation of `find_class` helper
 * method at https://developer.android.com/training/articles/perf-jni#native-libraries.
 *
 * The method works on Android targets only as the `set_class_loader` method is not
 * called by JVM-only targets.
 *
 * @param class_name, the name of the class to find (i.e.
 * "io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary").
 *
 * @return jclass, the class with a provided `class_name` or NULL if
 *         it couldn't be found.
 */
jclass find_class(const char* class_name);

void jni_delete_global_ref(void* context);

void jni_delete_const_global_ref(const void* context);

int unbox_integer(JniHelper& jni_helper, jobject boxedInteger);

envoy_data array_to_native_data(JniHelper& jni_helper, jbyteArray j_data);

envoy_data array_to_native_data(JniHelper& jni_helper, jbyteArray j_data, size_t data_length);

/**
 * Utility function that copies envoy_data to jbyteArray.
 *
 * @param env, the JNI env pointer.
 * @param envoy_data, the source to copy from.
 *
 * @return jbyteArray, copied data. It is up to the function caller to clean up memory.
 */
jbyteArray native_data_to_array(JniHelper& jni_helper, envoy_data data);

jlongArray native_stream_intel_to_array(JniHelper& jni_helper, envoy_stream_intel stream_intel);

jlongArray native_final_stream_intel_to_array(JniHelper& jni_helper,
                                              envoy_final_stream_intel final_stream_intel);

/**
 * Utility function that copies envoy_map to a java HashMap jobject.
 *
 * @param env, the JNI env pointer.
 * @param envoy_map, the source to copy from.
 *
 * @return jobject, copied data. It is up to the function caller to clean up memory.
 */
jobject native_map_to_map(JniHelper& jni_helper, envoy_map map);

jstring native_data_to_string(JniHelper& jni_helper, envoy_data data);

envoy_data buffer_to_native_data(JniHelper& jni_helper, jobject j_data);

envoy_data buffer_to_native_data(JniHelper& jni_helper, jobject j_data, size_t data_length);

envoy_data* buffer_to_native_data_ptr(JniHelper& jni_helper, jobject j_data);

envoy_headers to_native_headers(JniHelper& jni_helper, jobjectArray headers);

envoy_headers* to_native_headers_ptr(JniHelper& jni_helper, jobjectArray headers);

envoy_stats_tags to_native_tags(JniHelper& jni_helper, jobjectArray tags);

envoy_map to_native_map(JniHelper& jni_helper, jobjectArray entries);

/**
 * Utilities to translate C++ std library constructs to their Java counterpart.
 * The underlying data is always copied to disentangle C++ and Java objects lifetime.
 */
jobjectArray ToJavaArrayOfByteArray(JniHelper& jni_helper, const std::vector<std::string>& v);

jbyteArray ToJavaByteArray(JniHelper& jni_helper, const uint8_t* bytes, size_t len);

jbyteArray ToJavaByteArray(JniHelper& jni_helper, const std::string& str);

jobjectArray ToJavaArrayOfObjectArray(JniHelper& jni_helper,
                                      const Envoy::Types::ManagedEnvoyHeaders& map);

void JavaArrayOfByteArrayToStringVector(JniHelper& jni_helper, jobjectArray array,
                                        std::vector<std::string>* out);

void JavaArrayOfByteToBytesVector(JniHelper& jni_helper, jbyteArray array,
                                  std::vector<uint8_t>* out);

void JavaArrayOfByteToString(JniHelper& jni_helper, jbyteArray jbytes, std::string* out);

std::vector<MatcherData> javaObjectArrayToMatcherData(JniHelper& jni_helper, jobjectArray array,
                                                      std::string& cluster_out);

/** Parses the proto from Java's byte array and stores the output into `dest` proto. */
void javaByteArrayToProto(JniHelper& jni_helper, jbyteArray source,
                          Envoy::Protobuf::MessageLite* dest);

// TODO(fredyw): Delete these functions are replaced them with the ones from JniHelper

/** Throws Java exception with the specified class name and error message. */
void throwException(JniHelper& jni_helper, const char* java_class_name, const char* message);

// Helper functions for JNI's `Call<Type>Method` with proper exception handling in order to satisfy
// -Xcheck:jni.
// See
// https://docs.oracle.com/en/java/javase/11/docs/specs/jni/functions.html#calling-instance-methods
#define JNI_UTILITY_DECLARE_CALL_METHOD(JAVA_TYPE, JNI_TYPE)                                       \
  JNI_TYPE call##JAVA_TYPE##Method(JniHelper& jni_helper, jobject object, jmethodID method_id, ...);

void callVoidMethod(JniHelper& jni_helper, jobject object, jmethodID method_id, ...);
JNI_UTILITY_DECLARE_CALL_METHOD(Byte, jbyte)
JNI_UTILITY_DECLARE_CALL_METHOD(Char, jchar)
JNI_UTILITY_DECLARE_CALL_METHOD(Short, jshort)
JNI_UTILITY_DECLARE_CALL_METHOD(Int, jint)
JNI_UTILITY_DECLARE_CALL_METHOD(Long, jlong)
JNI_UTILITY_DECLARE_CALL_METHOD(Double, jdouble)
JNI_UTILITY_DECLARE_CALL_METHOD(Boolean, jboolean)
JNI_UTILITY_DECLARE_CALL_METHOD(Object, jobject)

// Helper functions for JNI's `CallStatic<Type>Method` with proper exception handling in order to
// satisfy -Xcheck:jni.
// See
// https://docs.oracle.com/en/java/javase/11/docs/specs/jni/functions.html#calling-static-methods
#define JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(JAVA_TYPE, JNI_TYPE)                                \
  JNI_TYPE callStatic##JAVA_TYPE##Method(JniHelper& jni_helper, jclass clazz, jmethodID method_id, \
                                         ...);

void callStaticVoidMethod(JniHelper& jni_helper, jclass clazz, jmethodID method_id, ...);
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Byte, jbyte)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Char, jchar)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Short, jshort)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Int, jint)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Long, jlong)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Double, jdouble)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Boolean, jboolean)
JNI_UTILITY_DECLARE_CALL_STATIC_METHOD(Object, jobject)

} // namespace JNI
} // namespace Envoy
