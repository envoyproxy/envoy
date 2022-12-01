#include "library/common/jni/jni_utility.h"

#include <stdlib.h>
#include <string.h>

#include "source/common/common/assert.h"

#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_version.h"

// NOLINT(namespace-envoy)

static JavaVM* static_jvm = nullptr;
static jobject static_class_loader = nullptr;
static thread_local JNIEnv* local_env = nullptr;

void set_vm(JavaVM* vm) { static_jvm = vm; }

JavaVM* get_vm() { return static_jvm; }

void set_class_loader(jobject class_loader) { static_class_loader = class_loader; }

jobject get_class_loader() {
  RELEASE_ASSERT(static_class_loader,
                 "find_class() is used before calling AndroidJniLibrary.load()");
  return static_class_loader;
}

jclass find_class(const char* class_name) {
  JNIEnv* env = get_env();
  jclass class_loader = env->FindClass("java/lang/ClassLoader");
  jmethodID find_class_method =
      env->GetMethodID(class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring str_class_name = env->NewStringUTF(class_name);
  jclass clazz =
      (jclass)(env->CallObjectMethod(get_class_loader(), find_class_method, str_class_name));
  env->DeleteLocalRef(str_class_name);
  return clazz;
}

JNIEnv* get_env() {
  if (local_env) {
    return local_env;
  }

  jint result = static_jvm->GetEnv(reinterpret_cast<void**>(&local_env), JNI_VERSION);
  if (result == JNI_EDETACHED) {
    // Note: the only thread that should need to be attached is Envoy's engine std::thread.
    static const char* thread_name = "EnvoyMain";
    JavaVMAttachArgs args = {JNI_VERSION, const_cast<char*>(thread_name), nullptr};
    result = attach_jvm(static_jvm, &local_env, &args);
  }
  RELEASE_ASSERT(result == JNI_OK, "Unable to get a JVM env for the current thread");
  return local_env;
}

void jvm_detach_thread() { static_jvm->DetachCurrentThread(); }

void jni_delete_global_ref(void* context) {
  JNIEnv* env = get_env();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

void jni_delete_const_global_ref(const void* context) {
  jni_delete_global_ref(const_cast<void*>(context));
}

int unbox_integer(JNIEnv* env, jobject boxedInteger) {
  jclass jcls_Integer = env->FindClass("java/lang/Integer");
  jmethodID jmid_intValue = env->GetMethodID(jcls_Integer, "intValue", "()I");
  env->DeleteLocalRef(jcls_Integer);
  return env->CallIntMethod(boxedInteger, jmid_intValue);
}

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data) {
  size_t data_length = static_cast<size_t>(env->GetArrayLength(j_data));
  return array_to_native_data(env, j_data, data_length);
}

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data, size_t data_length) {
  uint8_t* native_bytes = static_cast<uint8_t*>(safe_malloc(data_length));
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, 0);
  memcpy(native_bytes, critical_data, data_length); // NOLINT(safe-memcpy)
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return {data_length, native_bytes, free, native_bytes};
}

jstring native_data_to_string(JNIEnv* env, envoy_data data) {
  // Ensure we get a null-terminated string, the data coming in via envoy_data might not be.
  std::string str(reinterpret_cast<const char*>(data.bytes), data.length);
  jstring jstrBuf = env->NewStringUTF(str.c_str());
  return jstrBuf;
}

jbyteArray native_data_to_array(JNIEnv* env, envoy_data data) {
  jbyteArray j_data = env->NewByteArray(data.length);
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, nullptr);
  RELEASE_ASSERT(critical_data != nullptr, "unable to allocate memory in jni_utility");
  memcpy(critical_data, data.bytes, data.length); // NOLINT(safe-memcpy)
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  // TODO: potential perf improvement. Check if copied via isCopy, and optimize memory handling.
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return j_data;
}

jlongArray native_stream_intel_to_array(JNIEnv* env, envoy_stream_intel stream_intel) {
  jlongArray j_array = env->NewLongArray(4);
  jlong* critical_array = static_cast<jlong*>(env->GetPrimitiveArrayCritical(j_array, nullptr));
  RELEASE_ASSERT(critical_array != nullptr, "unable to allocate memory in jni_utility");
  critical_array[0] = static_cast<jlong>(stream_intel.stream_id);
  critical_array[1] = static_cast<jlong>(stream_intel.connection_id);
  critical_array[2] = static_cast<jlong>(stream_intel.attempt_count);
  critical_array[3] = static_cast<jlong>(stream_intel.consumed_bytes_from_response);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  env->ReleasePrimitiveArrayCritical(j_array, critical_array, 0);
  return j_array;
}

jlongArray native_final_stream_intel_to_array(JNIEnv* env,
                                              envoy_final_stream_intel final_stream_intel) {
  jlongArray j_array = env->NewLongArray(16);
  jlong* critical_array = static_cast<jlong*>(env->GetPrimitiveArrayCritical(j_array, nullptr));
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
  env->ReleasePrimitiveArrayCritical(j_array, critical_array, 0);
  return j_array;
}

jobject native_map_to_map(JNIEnv* env, envoy_map map) {
  jclass jcls_hashMap = env->FindClass("java/util/HashMap");
  jmethodID jmid_hashMapInit = env->GetMethodID(jcls_hashMap, "<init>", "(I)V");
  jobject j_hashMap = env->NewObject(jcls_hashMap, jmid_hashMapInit, map.length);
  jmethodID jmid_hashMapPut = env->GetMethodID(
      jcls_hashMap, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
  for (envoy_map_size_t i = 0; i < map.length; i++) {
    auto key = native_data_to_string(env, map.entries[i].key);
    auto value = native_data_to_string(env, map.entries[i].value);
    env->CallObjectMethod(j_hashMap, jmid_hashMapPut, key, value);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);
  }
  env->DeleteLocalRef(jcls_hashMap);
  return j_hashMap;
}

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data) {
  // Returns -1 if the buffer is not a direct buffer.
  jlong data_length = env->GetDirectBufferCapacity(j_data);

  if (data_length < 0) {
    jclass jcls_ByteBuffer = env->FindClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = env->GetMethodID(jcls_ByteBuffer, "array", "()[B");
    jbyteArray array = static_cast<jbyteArray>(env->CallObjectMethod(j_data, jmid_array));
    env->DeleteLocalRef(jcls_ByteBuffer);

    envoy_data native_data = array_to_native_data(env, array);
    env->DeleteLocalRef(array);
    return native_data;
  }

  return buffer_to_native_data(env, j_data, static_cast<size_t>(data_length));
}

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data, size_t data_length) {
  // Returns nullptr if the buffer is not a direct buffer.
  uint8_t* direct_address = static_cast<uint8_t*>(env->GetDirectBufferAddress(j_data));

  if (direct_address == nullptr) {
    jclass jcls_ByteBuffer = env->FindClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = env->GetMethodID(jcls_ByteBuffer, "array", "()[B");
    jbyteArray array = static_cast<jbyteArray>(env->CallObjectMethod(j_data, jmid_array));
    env->DeleteLocalRef(jcls_ByteBuffer);

    envoy_data native_data = array_to_native_data(env, array, data_length);
    env->DeleteLocalRef(array);
    return native_data;
  }

  envoy_data native_data;
  native_data.bytes = direct_address;
  native_data.length = data_length;
  native_data.release = jni_delete_global_ref;
  native_data.context = env->NewGlobalRef(j_data);

  return native_data;
}

envoy_data* buffer_to_native_data_ptr(JNIEnv* env, jobject j_data) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (j_data == nullptr) {
    return nullptr;
  }

  envoy_data* native_data = static_cast<envoy_data*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_data = buffer_to_native_data(env, j_data);
  return native_data;
}

envoy_headers to_native_headers(JNIEnv* env, jobjectArray headers) {
  return to_native_map(env, headers);
}

envoy_headers* to_native_headers_ptr(JNIEnv* env, jobjectArray headers) {
  // Note: This check works for LocalRefs and GlobalRefs, but will not work for WeakGlobalRefs.
  // Such usage would generally be inappropriate anyways; like C++ weak_ptrs, one should
  // acquire a new strong reference before attempting to interact with an object held by
  // a WeakGlobalRef. See:
  // https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#weak
  if (headers == nullptr) {
    return nullptr;
  }

  envoy_headers* native_headers = static_cast<envoy_headers*>(safe_malloc(sizeof(envoy_map_entry)));
  *native_headers = to_native_headers(env, headers);
  return native_headers;
}

envoy_stats_tags to_native_tags(JNIEnv* env, jobjectArray tags) { return to_native_map(env, tags); }

envoy_map to_native_map(JNIEnv* env, jobjectArray entries) {
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_map_entry.
  envoy_map_size_t length = env->GetArrayLength(entries);
  if (length == 0) {
    return {0, nullptr};
  }

  envoy_map_entry* entry_array =
      static_cast<envoy_map_entry*>(safe_malloc(sizeof(envoy_map_entry) * length / 2));

  for (envoy_map_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    jbyteArray j_key = static_cast<jbyteArray>(env->GetObjectArrayElement(entries, i));
    envoy_data entry_key = array_to_native_data(env, j_key);

    // Copy native byte array for header value
    jbyteArray j_value = static_cast<jbyteArray>(env->GetObjectArrayElement(entries, i + 1));
    envoy_data entry_value = array_to_native_data(env, j_value);

    entry_array[i / 2] = {entry_key, entry_value};
    env->DeleteLocalRef(j_key);
    env->DeleteLocalRef(j_value);
  }

  envoy_map native_map = {length / 2, entry_array};
  return native_map;
}

jobjectArray ToJavaArrayOfByteArray(JNIEnv* env, const std::vector<std::string>& v) {
  jclass jcls_byte_array = env->FindClass("[B");
  jobjectArray joa = env->NewObjectArray(v.size(), jcls_byte_array, nullptr);

  for (size_t i = 0; i < v.size(); ++i) {
    jbyteArray byte_array =
        ToJavaByteArray(env, reinterpret_cast<const uint8_t*>(v[i].data()), v[i].length());
    env->SetObjectArrayElement(joa, i, byte_array);
  }
  return joa;
}

jbyteArray ToJavaByteArray(JNIEnv* env, const uint8_t* bytes, size_t len) {
  jbyteArray byte_array = env->NewByteArray(len);
  const jbyte* jbytes = reinterpret_cast<const jbyte*>(bytes);
  env->SetByteArrayRegion(byte_array, /*start=*/0, len, jbytes);
  return byte_array;
}

jbyteArray ToJavaByteArray(JNIEnv* env, const std::string& str) {
  const uint8_t* str_bytes = reinterpret_cast<const uint8_t*>(str.data());
  return ToJavaByteArray(env, str_bytes, str.size());
}

void JavaArrayOfByteArrayToStringVector(JNIEnv* env, jobjectArray array,
                                        std::vector<std::string>* out) {
  ASSERT(out);
  ASSERT(array);
  size_t len = env->GetArrayLength(array);
  out->resize(len);

  for (size_t i = 0; i < len; ++i) {
    jbyteArray bytes_array = static_cast<jbyteArray>(env->GetObjectArrayElement(array, i));
    jsize bytes_len = env->GetArrayLength(bytes_array);
    // It doesn't matter if the array returned by GetByteArrayElements is a copy
    // or not, as the data will be simply be copied into C++ owned memory below.
    jbyte* bytes = env->GetByteArrayElements(bytes_array, /*isCopy=*/nullptr);
    (*out)[i].assign(reinterpret_cast<const char*>(bytes), bytes_len);
    // There is nothing to write back, it is always safe to JNI_ABORT.
    env->ReleaseByteArrayElements(bytes_array, bytes, JNI_ABORT);
    // Explicitly delete to keep the local ref count low.
    env->DeleteLocalRef(bytes_array);
  }
}

void JavaArrayOfByteToString(JNIEnv* env, jbyteArray jbytes, std::string* out) {
  std::vector<uint8_t> bytes;
  JavaArrayOfByteToBytesVector(env, jbytes, &bytes);
  *out = std::string(bytes.begin(), bytes.end());
}

void JavaArrayOfByteToBytesVector(JNIEnv* env, jbyteArray array, std::vector<uint8_t>* out) {
  const size_t len = env->GetArrayLength(array);
  out->resize(len);

  // It doesn't matter if the array returned by GetByteArrayElements is a copy
  // or not, as the data will be simply be copied into C++ owned memory below.
  jbyte* jbytes = env->GetByteArrayElements(array, /*isCopy=*/nullptr);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(jbytes);
  std::copy(bytes, bytes + len, out->begin());
  // There is nothing to write back, it is always safe to JNI_ABORT.
  env->ReleaseByteArrayElements(array, jbytes, JNI_ABORT);
}
