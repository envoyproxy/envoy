#include <android/log.h>
#include <ares.h>
#include <jni.h>

#include <string>

#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/jni/jni_version.h"
#include "library/common/main_interface.h"

// NOLINT(namespace-envoy)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION) != JNI_OK) {
    return -1;
  }

  set_vm(vm);
  return JNI_VERSION;
}

// JniLibrary

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env,
    jclass // class
) {
  return init_engine();
}

static void jvm_on_engine_running(void* context) {
  __android_log_write(ANDROID_LOG_VERBOSE, "[Envoy]", "jvm_on_engine_running");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  jclass jcls_JvmonEngineRunningContext = env->GetObjectClass(j_context);
  jmethodID jmid_onEngineRunning = env->GetMethodID(
      jcls_JvmonEngineRunningContext, "invokeOnEngineRunning", "()Ljava/lang/Object;");
  env->CallObjectMethod(j_context, jmid_onEngineRunning);

  // Delete the local ref since the engine does not use it directly.
  // If this changes in the future, deletion will need to be moved.
  env->DeleteLocalRef(jcls_JvmonEngineRunningContext);
  env->DeleteGlobalRef(j_context);
}

static void jvm_on_exit(void*) {
  __android_log_write(ANDROID_LOG_INFO, "[Envoy]", "library is exiting");
  // Note that this is not dispatched because the thread that
  // needs to be detached is the engine thread.
  // This function is called from the context of the engine's
  // thread due to it being posted to the engine's event dispatcher.
  jvm_detach_thread();
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(
    JNIEnv* env, jclass, jlong engine, jstring config, jstring log_level, jobject context) {
  jobject retained_context = env->NewGlobalRef(context); // Required to keep context in memory
  envoy_engine_callbacks native_callbacks = {jvm_on_engine_running, jvm_on_exit, retained_context};
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

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordCounter(JNIEnv* env,
                                                               jclass, // class
                                                               jstring elements, jint count) {
  record_counter(env->GetStringUTFChars(elements, nullptr), count);
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
// These methods call jvm methods which means the local references created will not be
// released automatically. Manual bookkeeping is required for these methods.

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

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_headers = to_native_headers(env, j_headers);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_headers);

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
}

static envoy_filter_headers_status
jvm_http_filter_on_response_headers(envoy_headers headers, bool end_stream, const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_headers("onResponseHeaders", headers, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_headers = to_native_headers(env, j_headers);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_headers);

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
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

  int unboxed_status = unbox_integer(env, status);
  envoy_data native_data = buffer_to_native_data(env, j_data);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_data);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data};
}

static envoy_filter_data_status jvm_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_data("onResponseData", data, end_stream, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobject j_data = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_data native_data = buffer_to_native_data(env, j_data);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_data);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data};
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

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_headers = to_native_headers(env, j_trailers);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_trailers);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_headers};
}

static envoy_filter_trailers_status jvm_http_filter_on_response_trailers(envoy_headers trailers,
                                                                         const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_trailers("onResponseTrailers", trailers, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_headers = to_native_headers(env, j_trailers);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_trailers);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_headers};
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
  env->DeleteLocalRef(jcls_JvmObserverContext);
  env->DeleteLocalRef(j_error_message);

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
  env->DeleteLocalRef(jcls_JvmObserverContext);
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

  env->DeleteLocalRef(jcls_JvmFilterFactoryContext);
  env->DeleteLocalRef(j_filter);

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
