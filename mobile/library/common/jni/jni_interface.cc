#include <android/log.h>
#include <ares.h>
#include <jni.h>

#include <string>

#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/main_interface.h"

static JavaVM* static_jvm = nullptr;
static JNIEnv* static_env = nullptr;
const static jint JNI_VERSION = JNI_VERSION_1_6;

// NOLINT(namespace-envoy)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  static_jvm = vm;
  if (vm->GetEnv((void**)&static_env, JNI_VERSION) != JNI_OK) {
    return -1;
  }

  return JNI_VERSION;
}

// JniLibrary

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env,
    jclass // class
) {
  return init_engine();
}

static void jvm_on_exit() {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "library is exiting");
  // Note that this is not dispatched because the thread that
  // needs to be detached is the engine thread.
  // This function is called from the context of the engine's
  // thread due to it being posted to the engine's event dispatcher.
  static_jvm->DetachCurrentThread();
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(
    JNIEnv* env, jclass, jlong engine, jstring config, jstring log_level) {
  envoy_engine_callbacks native_callbacks = {jvm_on_exit};
  return run_engine(engine, native_callbacks, env->GetStringUTFChars(config, nullptr),
                    env->GetStringUTFChars(log_level, nullptr));
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_templateString(JNIEnv* env,
                                                                jclass // class
) {
  jstring result = env->NewStringUTF(config_template);
  return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_filterTemplateString(JNIEnv* env,
                                                                      jclass // class
) {
  jstring result = env->NewStringUTF(platform_filter_template);
  return result;
}

// AndroidJniLibrary

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_initialize(JNIEnv* env,
                                                                   jclass, // class
                                                                   jobject connectivity_manager) {
  // See note above about c-ares.
  // c-ares jvm init is necessary in order to let c-ares perform DNS resolution in Envoy.
  // More information can be found at:
  // https://c-ares.haxx.se/ares_library_init_android.html
  ares_library_init_jvm(static_jvm);

  return ares_library_init_android(connectivity_manager);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_setPreferredNetwork(JNIEnv* env,
                                                                            jclass, // class
                                                                            jint network) {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "setting preferred network");
  return set_preferred_network(static_cast<envoy_network_t>(network));
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordCounter(JNIEnv* env,
                                                               jclass, // class
                                                               jstring elements, jint count) {
  record_counter(env->GetStringUTFChars(elements, nullptr), count);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_AndroidJniLibrary_flushStats(JNIEnv* env,
                                                                   jclass // class
) {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "triggering stats flush");
  flush_stats();
}

// Utility functions
static JNIEnv* get_env() {
  JNIEnv* env = nullptr;
  int get_env_res = static_jvm->GetEnv((void**)&env, JNI_VERSION);
  if (get_env_res == JNI_EDETACHED) {
    __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "environment is JNI_EDETACHED");
    // Note: the only thread that should need to be attached is Envoy's engine std::thread.
    // TODO: harden this piece of code to make sure that we are only needing to attach Envoy
    // engine's std::thread, and that we detach it successfully.
    static_jvm->AttachCurrentThread(&env, nullptr);
    static_jvm->GetEnv((void**)&env, JNI_VERSION);
  }
  return env;
}

static void jni_delete_global_ref(void* context) {
  JNIEnv* env = get_env();
  jobject ref = static_cast<jobject>(context);
  env->DeleteGlobalRef(ref);
}

static void jni_delete_const_global_ref(const void* context) {
  jni_delete_global_ref(const_cast<void*>(context));
}

static int unbox_integer(JNIEnv* env, jobject boxedInteger) {
  jclass jcls_Integer = env->FindClass("java/lang/Integer");
  jmethodID jmid_intValue = env->GetMethodID(jcls_Integer, "intValue", "()I");
  return env->CallIntMethod(boxedInteger, jmid_intValue);
}

static envoy_data array_to_native_data(JNIEnv* env, jbyteArray j_data) {
  size_t data_length = env->GetArrayLength(j_data);
  uint8_t* native_bytes = (uint8_t*)malloc(data_length);
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, 0);
  memcpy(native_bytes, critical_data, data_length);
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  return {data_length, native_bytes, free, native_bytes};
}

static envoy_data buffer_to_native_data(JNIEnv* env, jobject j_data) {
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

// JvmCallbackContext

static void pass_headers(JNIEnv* env, envoy_headers headers, jobject j_context) {
  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_passHeader = env->GetMethodID(jcls_JvmCallbackContext, "passHeader", "([B[BZ)V");
  env->PushLocalFrame(headers.length * 2);
  jboolean start_headers = JNI_TRUE;

  for (envoy_header_size_t i = 0; i < headers.length; i++) {
    // Note this is just an initial implementation, and we will pass a more optimized structure in
    // the future.

    // Note the JNI function NewStringUTF would appear to be an appealing option here, except it
    // requires a null-terminated *modified* UTF-8 string.

    // Create platform byte array for header key
    jbyteArray key = env->NewByteArray(headers.headers[i].key.length);
    // TODO: check if copied via isCopy.
    // TODO: check for NULL.
    // https://github.com/lyft/envoy-mobile/issues/758
    void* critical_key = env->GetPrimitiveArrayCritical(key, nullptr);
    memcpy(critical_key, headers.headers[i].key.bytes, headers.headers[i].key.length);
    // Here '0' (for which there is no named constant) indicates we want to commit the changes back
    // to the JVM and free the c array, where applicable.
    env->ReleasePrimitiveArrayCritical(key, critical_key, 0);

    // Create platform byte array for header value
    jbyteArray value = env->NewByteArray(headers.headers[i].value.length);
    // TODO: check for NULL.
    void* critical_value = env->GetPrimitiveArrayCritical(value, nullptr);
    memcpy(critical_value, headers.headers[i].value.bytes, headers.headers[i].value.length);
    env->ReleasePrimitiveArrayCritical(value, critical_value, 0);

    // Pass this header pair to the platform
    env->CallVoidMethod(j_context, jmid_passHeader, key, value, start_headers);

    // We don't release local refs currently because we've pushed a large enough frame, but we could
    // consider this and/or periodically popping the frame.
    start_headers = JNI_FALSE;
  }

  env->PopLocalFrame(nullptr);
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  release_envoy_headers(headers);
}

// Platform callback implementation
static void* jvm_on_headers(const char* method, envoy_headers headers, bool end_stream,
                            void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_headers");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  pass_headers(env, headers, j_context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onHeaders =
      env->GetMethodID(jcls_JvmCallbackContext, method, "(JZ)Ljava/lang/Object;");
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobject result = env->CallObjectMethod(j_context, jmid_onHeaders, (jlong)headers.length,
                                         end_stream ? JNI_TRUE : JNI_FALSE);

  env->DeleteLocalRef(jcls_JvmCallbackContext);
  return result;
}

static void* jvm_on_response_headers(envoy_headers headers, bool end_stream, void* context) {
  return jvm_on_headers("onResponseHeaders", headers, end_stream, context);
}

static envoy_filter_headers_status
jvm_http_filter_on_request_headers(envoy_headers headers, bool end_stream, const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_headers("onRequestHeaders", headers, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_headers_status){/*status*/ unbox_integer(env, status),
                                       /*headers*/ to_native_headers(env, j_headers)};
}

static envoy_filter_headers_status
jvm_http_filter_on_response_headers(envoy_headers headers, bool end_stream, const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_headers("onResponseHeaders", headers, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_headers_status){/*status*/ unbox_integer(env, status),
                                       /*headers*/ to_native_headers(env, j_headers)};
}

static void* jvm_on_data(const char* method, envoy_data data, bool end_stream, void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_data");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onData =
      env->GetMethodID(jcls_JvmCallbackContext, method, "([BZ)Ljava/lang/Object;");

  jbyteArray j_data = env->NewByteArray(data.length);
  // TODO: check if copied via isCopy.
  // TODO: check for NULL.
  // https://github.com/lyft/envoy-mobile/issues/758
  void* critical_data = env->GetPrimitiveArrayCritical(j_data, nullptr);
  memcpy(critical_data, data.bytes, data.length);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  env->ReleasePrimitiveArrayCritical(j_data, critical_data, 0);
  jobject result =
      env->CallObjectMethod(j_context, jmid_onData, j_data, end_stream ? JNI_TRUE : JNI_FALSE);

  data.release(data.context);
  env->DeleteLocalRef(j_data);
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  return result;
}

static void* jvm_on_response_data(envoy_data data, bool end_stream, void* context) {
  return jvm_on_data("onResponseData", data, end_stream, context);
}

static envoy_filter_data_status jvm_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_data("onRequestData", data, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobject j_data = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_data_status){/*status*/ unbox_integer(env, status),
                                    /*data*/ buffer_to_native_data(env, j_data)};
}

static envoy_filter_data_status jvm_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_data("onResponseData", data, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobject j_data = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_data_status){/*status*/ unbox_integer(env, status),
                                    /*data*/ buffer_to_native_data(env, j_data)};
}

static void* jvm_on_metadata(envoy_headers metadata, void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_metadata");
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", std::to_string(metadata.length).c_str());
  return NULL;
}

static void* jvm_on_trailers(const char* method, envoy_headers trailers, void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_trailers");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  pass_headers(env, trailers, j_context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onTrailers =
      env->GetMethodID(jcls_JvmCallbackContext, method, "(J)Ljava/lang/Object;");
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobject result = env->CallObjectMethod(j_context, jmid_onTrailers, (jlong)trailers.length);

  env->DeleteLocalRef(jcls_JvmCallbackContext);
  return result;
}

static void* jvm_on_response_trailers(envoy_headers trailers, void* context) {
  return jvm_on_trailers("onResponseTrailers", trailers, context);
}

static envoy_filter_trailers_status jvm_http_filter_on_request_trailers(envoy_headers trailers,
                                                                        const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_trailers("onRequestTrailers", trailers, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_trailers_status){/*status*/ unbox_integer(env, status),
                                        /*trailers*/ to_native_headers(env, j_trailers)};
}

static envoy_filter_trailers_status jvm_http_filter_on_response_trailers(envoy_headers trailers,
                                                                         const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_trailers("onResponseTrailers", trailers, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  return (envoy_filter_trailers_status){/*status*/ unbox_integer(env, status),
                                        /*trailers*/ to_native_headers(env, j_trailers)};
}

static void* jvm_on_error(envoy_error error, void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_error");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmObserverContext = env->GetObjectClass(j_context);
  jmethodID jmid_onError =
      env->GetMethodID(jcls_JvmObserverContext, "onError", "(I[BI)Ljava/lang/Object;");

  jbyteArray j_error_message = env->NewByteArray(error.message.length);
  // TODO: check if copied via isCopy.
  // TODO: check for NULL.
  // https://github.com/lyft/envoy-mobile/issues/758
  void* critical_error_message = env->GetPrimitiveArrayCritical(j_error_message, nullptr);
  memcpy(critical_error_message, error.message.bytes, error.message.length);
  // Here '0' (for which there is no named constant) indicates we want to commit the changes back
  // to the JVM and free the c array, where applicable.
  env->ReleasePrimitiveArrayCritical(j_error_message, critical_error_message, 0);

  jobject result = env->CallObjectMethod(j_context, jmid_onError, error.error_code, j_error_message,
                                         error.attempt_count);

  error.message.release(error.message.context);
  // No further callbacks happen on this context. Delete the reference held by native code.
  env->DeleteGlobalRef(j_context);
  return result;
}

static void* jvm_on_complete(void* context) {
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  env->DeleteGlobalRef(j_context);
  return NULL;
}

static void* jvm_on_cancel(void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_cancel");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmObserverContext = env->GetObjectClass(j_context);
  jmethodID jmid_onCancel =
      env->GetMethodID(jcls_JvmObserverContext, "onCancel", "()Ljava/lang/Object;");
  jobject result = env->CallObjectMethod(j_context, jmid_onCancel);

  // No further callbacks happen on this context. Delete the reference held by native code.
  env->DeleteGlobalRef(j_context);
  return result;
}

// JvmFilterFactoryContext

static const void* jvm_http_filter_init(const void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_filter_init");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  __android_log_print(ANDROID_LOG_VERBOSE, "[Envoy]", "j_context: %p", j_context);

  jclass jcls_JvmFilterFactoryContext = env->GetObjectClass(j_context);
  jmethodID jmid_create = env->GetMethodID(jcls_JvmFilterFactoryContext, "create",
                                           "()Lio/envoyproxy/envoymobile/engine/JvmFilterContext;");

  jobject j_filter = env->CallObjectMethod(j_context, jmid_create);
  __android_log_print(ANDROID_LOG_VERBOSE, "[Envoy]", "j_filter: %p", j_filter);
  jobject retained_filter = env->NewGlobalRef(j_filter);
  return retained_filter;
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initStream(
    JNIEnv* env, jclass, jlong engine_handle) {

  return init_stream(static_cast<envoy_engine_t>(engine_handle));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_startStream(
    JNIEnv* env, jclass, jlong stream_handle, jobject j_context) {

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);

  // TODO: To be truly safe we may need stronger guarantees of operation ordering on this ref.
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_callbacks native_callbacks = {jvm_on_response_headers,
                                           jvm_on_response_data,
                                           jvm_on_metadata,
                                           jvm_on_response_trailers,
                                           jvm_on_error,
                                           jvm_on_complete,
                                           jvm_on_cancel,
                                           retained_context};
  envoy_status_t result =
      start_stream(static_cast<envoy_stream_t>(stream_handle), native_callbacks);
  if (result != ENVOY_SUCCESS) {
    env->DeleteGlobalRef(retained_context); // No callbacks are fired and we need to release
  }
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  return result;
}

// EnvoyHTTPFilter

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerFilterFactory(JNIEnv* env, jclass,
                                                                       jstring filter_name,
                                                                       jobject j_context) {

  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/lyft/envoy-mobile/issues/332
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "registerFilterFactory");
  __android_log_print(ANDROID_LOG_VERBOSE, "[Envoy]", "j_context: %p", j_context);
  jclass jcls_JvmFilterFactoryContext = env->GetObjectClass(j_context);
  jobject retained_context = env->NewGlobalRef(j_context);
  __android_log_print(ANDROID_LOG_VERBOSE, "[Envoy]", "retained_context: %p", retained_context);
  envoy_http_filter* api = (envoy_http_filter*)safe_malloc(sizeof(envoy_http_filter));
  api->init_filter = jvm_http_filter_init;
  api->on_request_headers = jvm_http_filter_on_request_headers;
  api->on_request_data = jvm_http_filter_on_request_data;
  api->on_request_trailers = jvm_http_filter_on_request_trailers;
  api->on_response_headers = jvm_http_filter_on_response_headers;
  api->on_response_data = jvm_http_filter_on_response_data;
  api->on_response_trailers = jvm_http_filter_on_response_trailers;
  api->release_filter = jni_delete_const_global_ref;
  api->static_context = retained_context;
  api->instance_context = NULL;

  register_platform_api(env->GetStringUTFChars(filter_name, nullptr), api);
  env->DeleteLocalRef(jcls_JvmFilterFactoryContext);
  return ENVOY_SUCCESS;
}

// Note: JLjava_nio_ByteBuffer_2Z is the mangled signature of the java method.
// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/design.html
extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData__JLjava_nio_ByteBuffer_2Z(
    JNIEnv* env, jclass, jlong stream_handle, jobject data, jboolean end_stream) {

  // TODO: check for null pointer in envoy_data.bytes - we could copy or raise an exception.
  return send_data(static_cast<envoy_stream_t>(stream_handle), buffer_to_native_data(env, data),
                   end_stream);
}

// Note: J_3BZ is the mangled signature of the java method.
// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/design.html
extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData__J_3BZ(
    JNIEnv* env, jclass, jlong stream_handle, jbyteArray data, jboolean end_stream) {

  // TODO: check for null pointer in envoy_data.bytes - we could copy or raise an exception.
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
