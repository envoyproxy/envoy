#include <android/log.h>
#include <ares.h>
#include <jni.h>

#include <string>

#include "main_interface.h"

static JavaVM* static_jvm = nullptr;
static JNIEnv* static_env = nullptr;
const static jint JNI_VERSION = JNI_VERSION_1_6;

// NOLINT(namespace-envoy)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  static_jvm = vm;
  if (vm->GetEnv((void**)&static_env, JNI_VERSION) != JNI_OK) {
    return -1;
  }

  // c-ares jvm init is necessary in order to let c-ares perform DNS resolution in Envoy.
  // More information can be found at:
  // https://c-ares.haxx.se/ares_library_init_android.html
  ares_library_init_jvm(vm);
  return JNI_VERSION;
}

// JniLibrary

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env,
    jclass // class
) {
  return init_engine();
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(JNIEnv* env,
                                                           jclass, // class
                                                           jstring config, jstring log_level) {
  return run_engine(env->GetStringUTFChars(config, nullptr),
                    env->GetStringUTFChars(log_level, nullptr));
}

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject connectivity_manager) {
  // See note above about c-ares.
  return ares_library_init_android(connectivity_manager);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_isAresInitialized(JNIEnv* env,
                                                                          jclass // class
) {
  return ares_library_android_initialized() == ARES_SUCCESS;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_setPreferredNetwork(JNIEnv* env,
                                                                            jclass, // class
                                                                            jint network) {
  return set_preferred_network(static_cast<envoy_network_t>(network));
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_templateString(JNIEnv* env,
                                                                jclass // class
) {
  jstring result = env->NewStringUTF(config_template);
  return result;
}

// JvmCallbackContext

static void pass_headers(JNIEnv* env, envoy_headers headers, jobject j_context) {
  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_passHeader = env->GetMethodID(jcls_JvmCallbackContext, "passHeader", "([B[BZ)V");
  env->PushLocalFrame(headers.length * 2);
  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    // Note this is just an initial implementation, and we will pass a more optimized structure in
    // the future.

    // Note the JNI function NewStringUTF would appear to be an appealing option here, except it
    // requires a null-terminated *modified* UTF-8 string.

    // Create platform byte array for header key
    jbyteArray key = env->NewByteArray(headers.headers[i].key.length);
    // FIXME: check if copied via isCopy
    void* critical_key = env->GetPrimitiveArrayCritical(key, nullptr); // FIXME: check for NULL
    memcpy(critical_key, headers.headers[i].key.bytes, headers.headers[i].key.length);
    // Here '0' (for which there is no named constant) indicates we want to commit the changes back
    // to the JVM and free the c array, where applicable.
    env->ReleasePrimitiveArrayCritical(key, critical_key, 0);

    // Create platform byte array for header value
    jbyteArray value = env->NewByteArray(headers.headers[i].value.length);
    void* critical_value = env->GetPrimitiveArrayCritical(value, nullptr); // FIXME: check for NULL
    memcpy(critical_value, headers.headers[i].value.bytes, headers.headers[i].value.length);
    env->ReleasePrimitiveArrayCritical(value, critical_value, 0);

    // Pass this header pair to the platform
    jboolean end_headers = i == headers.length - 1 ? JNI_TRUE : JNI_FALSE;
    env->CallVoidMethod(j_context, jmid_passHeader, key, value, end_headers);

    // We don't release local refs currently because we've pushed a large enough frame, but we could
    // consider this and/or periodically popping the frame.
  }
  env->PopLocalFrame(nullptr);
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  release_envoy_headers(headers);
}

// Platform callback implementation
static JNIEnv* get_env() {
  JNIEnv* env = nullptr;
  int get_env_res = static_jvm->GetEnv((void**)&env, JNI_VERSION);
  if (get_env_res == JNI_EDETACHED) {
    __android_log_write(ANDROID_LOG_ERROR, "jni_lib", "equals JNI_EDETACHED");
  }
  return env;
}

static void jvm_on_headers(envoy_headers headers, bool end_stream, void* context) {
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onHeaders = env->GetMethodID(jcls_JvmCallbackContext, "onHeaders", "(JZ)V");
  // Note: be careful of JVM types
  // FIXME: make this cast safer
  env->CallVoidMethod(j_context, jmid_onHeaders, (jlong)headers.length,
                      end_stream ? JNI_TRUE : JNI_FALSE);

  env->DeleteLocalRef(jcls_JvmCallbackContext);
  pass_headers(env, headers, j_context);
}

static void jvm_on_data(envoy_data data, bool end_stream, void* context) {
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", "jvm_on_data");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onData = env->GetMethodID(jcls_JvmCallbackContext, "onData", "([BZ)V");

  jbyteArray j_data = env->NewByteArray(data.length);
  // FIXME: check if copied via isCopy
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, nullptr); // FIXME: check for NULL
  memcpy(critical_data, data.bytes, data.length);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  env->CallVoidMethod(j_context, jmid_onData, j_data, end_stream ? JNI_TRUE : JNI_FALSE);

  data.release(data.context);
  env->DeleteLocalRef(j_data);
  env->DeleteLocalRef(jcls_JvmCallbackContext);
}

static void jvm_on_metadata(envoy_headers metadata, void* context) {
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", "jvm_on_metadata");
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", std::to_string(metadata.length).c_str());
}

static void jvm_on_trailers(envoy_headers trailers, void* context) {
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", "jvm_on_trailers");
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", std::to_string(trailers.length).c_str());
}

static void jvm_on_error(envoy_error error, void* context) {
  __android_log_write(ANDROID_LOG_ERROR, "jni_lib", "jvm_on_error");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmObserverContext = env->GetObjectClass(j_context);
  jmethodID jmid_onError = env->GetMethodID(jcls_JvmObserverContext, "onError", "([BI)V");

  jbyteArray j_error_message = env->NewByteArray(error.message.length);
  // FIXME: check if copied via isCopy
  void* critical_error_message =
      env->GetPrimitiveArrayCritical(j_error_message, nullptr); // FIXME: check for NULL
  memcpy(critical_error_message, error.message.bytes, error.message.length);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  env->ReleasePrimitiveArrayCritical(j_error_message, critical_error_message, 0);

  env->CallVoidMethod(j_context, jmid_onError, j_error_message, error.error_code);

  error.message.release(error.message.context);
  // No further callbacks happen on this context. Delete the reference held by native code.
  env->DeleteGlobalRef(j_context);
}

static void jvm_on_complete(void* context) {
  JNIEnv* env = nullptr;
  static_jvm->GetEnv((void**)&env, JNI_VERSION);
  jobject j_context = static_cast<jobject>(context);
  env->DeleteGlobalRef(j_context);
}

// Utility functions
static void jni_delete_global_ref(void* context) {
  JNIEnv* env = get_env();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

static envoy_data buffer_to_native_data(JNIEnv* env, jobject data) {
  jobject j_data = env->NewGlobalRef(data);
  envoy_data native_data;
  native_data.bytes = static_cast<uint8_t*>(env->GetDirectBufferAddress(j_data));
  native_data.length = env->GetDirectBufferCapacity(j_data);
  native_data.release = jni_delete_global_ref;
  native_data.context = j_data;

  return native_data;
}

static envoy_data array_to_native_data(JNIEnv* env, jbyteArray data) {
  size_t data_length = env->GetArrayLength(data);
  uint8_t* native_bytes = (uint8_t*)malloc(data_length);
  void* critical_data = env->GetPrimitiveArrayCritical(data, 0);
  memcpy(native_bytes, critical_data, data_length);
  env->ReleasePrimitiveArrayCritical(data, critical_data, 0);
  return {data_length, native_bytes, free, native_bytes};
}

static envoy_headers to_native_headers(JNIEnv* env, jobjectArray headers) {
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_header.
  envoy_header_size_t length = env->GetArrayLength(headers);
  envoy_header* header_array = (envoy_header*)safe_malloc(sizeof(envoy_header) * length / 2);

  for (envoy_header_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    jbyteArray j_key = (jbyteArray)env->GetObjectArrayElement(headers, i);
    size_t key_length = env->GetArrayLength(j_key);
    uint8_t* native_key = (uint8_t*)safe_malloc(key_length);
    void* critical_key = env->GetPrimitiveArrayCritical(j_key, 0);
    memcpy(native_key, critical_key, key_length);
    env->ReleasePrimitiveArrayCritical(j_key, critical_key, 0);
    envoy_data header_key = {key_length, native_key, free, native_key};

    // Copy native byte array for header value
    jbyteArray j_value = (jbyteArray)env->GetObjectArrayElement(headers, i + 1);
    size_t value_length = env->GetArrayLength(j_value);
    uint8_t* native_value = (uint8_t*)safe_malloc(value_length);
    void* critical_value = env->GetPrimitiveArrayCritical(j_value, 0);
    memcpy(native_value, critical_value, value_length);
    env->ReleasePrimitiveArrayCritical(j_value, critical_value, 0);
    envoy_data header_value = {value_length, native_value, free, native_value};

    header_array[i / 2] = {header_key, header_value};
  }

  envoy_headers native_headers = {length / 2, header_array};
  return native_headers;
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initStream(
    JNIEnv* env, jclass, jlong engine_handle) {

  return init_stream(static_cast<envoy_engine_t>(engine_handle));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_startStream(
    JNIEnv* env, jclass, jlong stream_handle, jobject j_context) {

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onHeaders = env->GetMethodID(jcls_JvmCallbackContext, "onHeaders", "(JZ)V");
  // TODO: To be truly safe we may need stronger guarantees of operation ordering on this ref
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_callbacks native_callbacks = {jvm_on_headers,  jvm_on_data,  jvm_on_metadata,
                                           jvm_on_trailers, jvm_on_error, jvm_on_complete,
                                           retained_context};
  envoy_status_t result =
      start_stream(static_cast<envoy_stream_t>(stream_handle), native_callbacks);
  if (result != ENVOY_SUCCESS) {
    env->DeleteGlobalRef(retained_context); // No callbacks are fired and we need to release
  }
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  return result;
}

// Note: JLjava_nio_ByteBuffer_2Z is the mangled signature of the java method.
// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/design.html
extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData__JLjava_nio_ByteBuffer_2Z(
    JNIEnv* env, jclass, jlong stream_handle, jobject data, jboolean end_stream) {

  // TODO: check for null pointer in envoy_data.bytes - we could copy or raise an exception
  return send_data(static_cast<envoy_stream_t>(stream_handle), buffer_to_native_data(env, data),
                   end_stream);
}

// Note: J_3BZ is the mangled signature of the java method.
// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/design.html
extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData__J_3BZ(
    JNIEnv* env, jclass, jlong stream_handle, jbyteArray data, jboolean end_stream) {

  // TODO: check for null pointer in envoy_data.bytes - we could copy or raise an exception
  return send_data(static_cast<envoy_stream_t>(stream_handle), array_to_native_data(env, data),
                   end_stream);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendHeaders(
    JNIEnv* env, jclass, jlong stream_handle, jobjectArray headers, jboolean end_stream) {

  return send_headers(static_cast<envoy_stream_t>(stream_handle), to_native_headers(env, headers),
                      end_stream);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendTrailers(
    JNIEnv* env, jclass, jlong stream_handle, jobjectArray trailers) {

  return send_trailers(static_cast<envoy_stream_t>(stream_handle),
                       to_native_headers(env, trailers));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetStream(
    JNIEnv* env, jclass, jlong stream_handle) {

  return reset_stream(static_cast<envoy_stream_t>(stream_handle));
}
