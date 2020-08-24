#include "library/common/jni/jni_utility.h"

#include <stdlib.h>
#include <string.h>

#include "library/common/jni/jni_version.h"

// NOLINT(namespace-envoy)

static JavaVM* static_jvm = nullptr;

void set_vm(JavaVM* vm) { static_jvm = vm; }

JavaVM* get_vm() { return static_jvm; }

JNIEnv* get_env() {
  JNIEnv* env = nullptr;
  int get_env_res = static_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION);
  if (get_env_res == JNI_EDETACHED) {
    // TODO(goway): fix logging
    // log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "environment is JNI_EDETACHED");
    // Note: the only thread that should need to be attached is Envoy's engine std::thread.
    // TODO: harden this piece of code to make sure that we are only needing to attach Envoy
    // engine's std::thread, and that we detach it successfully.
    static_jvm->AttachCurrentThread(&env, nullptr);
    static_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION);
  }
  return env;
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
  return env->CallIntMethod(boxedInteger, jmid_intValue);
}

envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data) {
  size_t data_length = env->GetArrayLength(j_data);
  uint8_t* native_bytes = static_cast<uint8_t*>(malloc(data_length));
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, 0);
  memcpy(native_bytes, critical_data, data_length);
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return {data_length, native_bytes, free, native_bytes};
}

envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data) {
  uint8_t* direct_address = static_cast<uint8_t*>(env->GetDirectBufferAddress(j_data));

  if (direct_address == nullptr) {
    jclass jcls_ByteBuffer = env->FindClass("java/nio/ByteBuffer");
    // We skip checking hasArray() because only direct ByteBuffers or array-backed ByteBuffers
    // are supported. We will crash here if this is an invalid buffer, but guards may be
    // implemented in the JVM layer.
    jmethodID jmid_array = env->GetMethodID(jcls_ByteBuffer, "array", "()[B");
    jbyteArray array = static_cast<jbyteArray>(env->CallObjectMethod(j_data, jmid_array));
    return array_to_native_data(env, array);
  }

  envoy_data native_data;
  native_data.bytes = direct_address;
  native_data.length = env->GetDirectBufferCapacity(j_data);
  native_data.release = jni_delete_global_ref;
  native_data.context = env->NewGlobalRef(j_data);

  return native_data;
}

envoy_headers to_native_headers(JNIEnv* env, jobjectArray headers) {
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_header.
  envoy_header_size_t length = env->GetArrayLength(headers);
  envoy_header* header_array =
      static_cast<envoy_header*>(safe_malloc(sizeof(envoy_header) * length / 2));

  for (envoy_header_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    jbyteArray j_key = static_cast<jbyteArray>(env->GetObjectArrayElement(headers, i));
    size_t key_length = env->GetArrayLength(j_key);
    uint8_t* native_key = static_cast<uint8_t*>(safe_malloc(key_length));
    void* critical_key = env->GetPrimitiveArrayCritical(j_key, 0);
    memcpy(native_key, critical_key, key_length);
    env->ReleasePrimitiveArrayCritical(j_key, critical_key, 0);
    envoy_data header_key = {key_length, native_key, free, native_key};

    // Copy native byte array for header value
    jbyteArray j_value = static_cast<jbyteArray>(env->GetObjectArrayElement(headers, i + 1));
    size_t value_length = env->GetArrayLength(j_value);
    uint8_t* native_value = static_cast<uint8_t*>(safe_malloc(value_length));
    void* critical_value = env->GetPrimitiveArrayCritical(j_value, 0);
    memcpy(native_value, critical_value, value_length);
    env->ReleasePrimitiveArrayCritical(j_value, critical_value, 0);
    envoy_data header_value = {value_length, native_value, free, native_value};

    header_array[i / 2] = {header_key, header_value};
  }

  envoy_headers native_headers = {length / 2, header_array};
  return native_headers;
}
