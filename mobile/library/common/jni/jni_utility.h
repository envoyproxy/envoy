#pragma once

#include <string>
#include <vector>

#include "library/common/jni/import/jni_import.h"
#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

void set_vm(JavaVM* vm);

JavaVM* get_vm();

JNIEnv* get_env();

void set_class_loader(jobject class_loader);

/**
 * Finds a class with a given name using a class loader provided with the use
 * of `set_class_loader` function. The class loader is supposed to come from
 * application's context and should be associated with project's code - Java classes
 * defined by the project. For finding classes of Java built in-types use
 * `env->FindClass(...)` method instead as it is lighter to use.
 *
 * The method works on Android targets only as the `set_class_loader` method is not
 * called by JVM-only targets.
 *
 * @param class_name, the name of the class to find (i.e. "org/chromium/net/AndroidNetworkLibrary").
 *
 * @return jclass, the class with a provided `class_name` or NULL if
 *         it couldn't be found.
 */
jclass find_class(const char* class_name);

void jvm_detach_thread();

void jni_delete_global_ref(void* context);

void jni_delete_const_global_ref(const void* context);

int unbox_integer(JNIEnv* env, jobject boxedInteger);

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data);

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data, size_t data_length);

/**
 * Utility function that copies envoy_data to jbyteArray.
 *
 * @param env, the JNI env pointer.
 * @param envoy_data, the source to copy from.
 *
 * @return jbyteArray, copied data. It is up to the function caller to clean up memory.
 */
jbyteArray native_data_to_array(JNIEnv* env, envoy_data data);

jlongArray native_stream_intel_to_array(JNIEnv* env, envoy_stream_intel stream_intel);

jlongArray native_final_stream_intel_to_array(JNIEnv* env,
                                              envoy_final_stream_intel final_stream_intel);

/**
 * Utility function that copies envoy_map to a java HashMap jobject.
 *
 * @param env, the JNI env pointer.
 * @param envoy_map, the source to copy from.
 *
 * @return jobject, copied data. It is up to the function caller to clean up memory.
 */
jobject native_map_to_map(JNIEnv* env, envoy_map map);

jstring native_data_to_string(JNIEnv* env, envoy_data data);

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data);

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data, size_t data_length);

envoy_data* buffer_to_native_data_ptr(JNIEnv* env, jobject j_data);

envoy_headers to_native_headers(JNIEnv* env, jobjectArray headers);

envoy_headers* to_native_headers_ptr(JNIEnv* env, jobjectArray headers);

envoy_stats_tags to_native_tags(JNIEnv* env, jobjectArray tags);

envoy_map to_native_map(JNIEnv* env, jobjectArray entries);

/**
 * Utilities to translate C++ std library constructs to their Java counterpart.
 * The underlying data is always copied to disentangle C++ and Java objects lifetime.
 */
jobjectArray ToJavaArrayOfByteArray(JNIEnv* env, const std::vector<std::string>& v);

jbyteArray ToJavaByteArray(JNIEnv* env, const uint8_t* bytes, size_t len);

jbyteArray ToJavaByteArray(JNIEnv* env, const std::string& str);

void JavaArrayOfByteArrayToStringVector(JNIEnv* env, jobjectArray array,
                                        std::vector<std::string>* out);

void JavaArrayOfByteToBytesVector(JNIEnv* env, jbyteArray array, std::vector<uint8_t>* out);

void JavaArrayOfByteToString(JNIEnv* env, jbyteArray jbytes, std::string* out);
