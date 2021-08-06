#include <ares.h>
#include <jni.h>

#include <string>

#include "library/common/api/c_types.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/jni/jni_support.h"
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

static void jvm_on_engine_running(void* context) {
  if (context == nullptr) {
    return;
  }

  jni_log("[Envoy]", "jvm_on_engine_running");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  jclass jcls_JvmonEngineRunningContext = env->GetObjectClass(j_context);
  jmethodID jmid_onEngineRunning = env->GetMethodID(
      jcls_JvmonEngineRunningContext, "invokeOnEngineRunning", "()Ljava/lang/Object;");
  env->CallObjectMethod(j_context, jmid_onEngineRunning);

  env->DeleteLocalRef(jcls_JvmonEngineRunningContext);
  // TODO(goaway): This isn't re-used by other engine callbacks, so it's safe to delete here.
  // This will need to be updated for https://github.com/lyft/envoy-mobile/issues/332
  env->DeleteGlobalRef(j_context);
}

static void jvm_on_log(envoy_data data, const void* context) {
  if (context == nullptr) {
    return;
  }

  JNIEnv* env = get_env();
  jstring str = native_data_to_string(env, data);

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jclass jcls_JvmLoggerContext = env->GetObjectClass(j_context);
  jmethodID jmid_onLog = env->GetMethodID(jcls_JvmLoggerContext, "log", "(Ljava/lang/String;)V");
  env->CallVoidMethod(j_context, jmid_onLog, str);

  env->DeleteLocalRef(str);
  env->DeleteLocalRef(jcls_JvmLoggerContext);
}

static void jvm_on_exit(void*) {
  jni_log("[Envoy]", "library is exiting");
  // Note that this is not dispatched because the thread that
  // needs to be detached is the engine thread.
  // This function is called from the context of the engine's
  // thread due to it being posted to the engine's event dispatcher.
  jvm_detach_thread();
}

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env, jclass, jobject on_start_context, jobject envoy_logger_context) {
  jobject retained_on_start_context =
      env->NewGlobalRef(on_start_context); // Required to keep context in memory
  envoy_engine_callbacks native_callbacks = {jvm_on_engine_running, jvm_on_exit,
                                             retained_on_start_context};

  const jobject retained_logger_context = env->NewGlobalRef(envoy_logger_context);
  envoy_logger logger = {nullptr, nullptr, nullptr};
  if (envoy_logger_context != nullptr) {
    logger = envoy_logger{jvm_on_log, jni_delete_const_global_ref, retained_logger_context};
  }
  return init_engine(native_callbacks, logger);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(
    JNIEnv* env, jclass, jlong engine, jstring config, jstring log_level) {
  return run_engine(engine, env->GetStringUTFChars(config, nullptr),
                    env->GetStringUTFChars(log_level, nullptr));
}

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_terminateEngine(
    JNIEnv* env, jclass, jlong engine_handle) {
  terminate_engine(static_cast<envoy_engine_t>(engine_handle));
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_templateString(JNIEnv* env,
                                                                jclass // class
) {
  jstring result = env->NewStringUTF(config_template);
  return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_platformFilterTemplateString(JNIEnv* env,
                                                                              jclass // class
) {
  jstring result = env->NewStringUTF(platform_filter_template);
  return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_nativeFilterTemplateString(JNIEnv* env,
                                                                            jclass // class
) {
  jstring result = env->NewStringUTF(native_filter_template);
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordCounterInc(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring elements, jobjectArray tags, jint count) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_counter_inc(engine, native_elements, to_native_tags(env, tags), count);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordGaugeSet(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring elements, jobjectArray tags, jint value) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_gauge_set(engine, native_elements, to_native_tags(env, tags), value);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordGaugeAdd(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring elements, jobjectArray tags, jint amount) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_gauge_add(engine, native_elements, to_native_tags(env, tags), amount);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordGaugeSub(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring elements, jobjectArray tags, jint amount) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_gauge_sub(engine, native_elements, to_native_tags(env, tags), amount);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordHistogramDuration(JNIEnv* env,
                                                                         jclass, // class
                                                                         jlong engine,
                                                                         jstring elements,
                                                                         jobjectArray tags,
                                                                         jint durationMs) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_histogram_value(engine, native_elements, to_native_tags(env, tags),
                                       durationMs, MILLISECONDS);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_flushStats(JNIEnv* env,
                                                            jclass, // class
                                                            jlong engine) {
  jni_log("[Envoy]", "flushStats");
  flush_stats(engine);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordHistogramValue(JNIEnv* env,
                                                                      jclass, // class
                                                                      jlong engine,
                                                                      jstring elements,
                                                                      jobjectArray tags,
                                                                      jint value) {
  const char* native_elements = env->GetStringUTFChars(elements, nullptr);
  jint result = record_histogram_value(engine, native_elements, to_native_tags(env, tags), value,
                                       UNSPECIFIED);
  env->ReleaseStringUTFChars(elements, native_elements);
  return result;
}

// JvmCallbackContext

static void pass_headers(const char* method, envoy_headers headers, jobject j_context) {
  JNIEnv* env = get_env();
  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_passHeader = env->GetMethodID(jcls_JvmCallbackContext, method, "([B[BZ)V");
  jboolean start_headers = JNI_TRUE;

  for (envoy_map_size_t i = 0; i < headers.length; i++) {
    // Note: this is just an initial implementation, and we will pass a more optimized structure in
    // the future.
    // Note: the JNI function NewStringUTF would appear to be an appealing option here, except it
    // requires a null-terminated *modified* UTF-8 string.

    // Create platform byte array for header key
    jbyteArray j_key = native_data_to_array(env, headers.entries[i].key);
    // Create platform byte array for header value
    jbyteArray j_value = native_data_to_array(env, headers.entries[i].value);

    // Pass this header pair to the platform
    env->CallVoidMethod(j_context, jmid_passHeader, j_key, j_value, start_headers);
    env->DeleteLocalRef(j_key);
    env->DeleteLocalRef(j_value);

    // We don't release local refs currently because we've pushed a large enough frame, but we could
    // consider this and/or periodically popping the frame.
    start_headers = JNI_FALSE;
  }

  env->DeleteLocalRef(jcls_JvmCallbackContext);
  release_envoy_headers(headers);
}

// Platform callback implementation
// These methods call jvm methods which means the local references created will not be
// released automatically. Manual bookkeeping is required for these methods.

static void* jvm_on_headers(const char* method, envoy_headers headers, bool end_stream,
                            envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_headers");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  pass_headers("passHeader", headers, j_context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onHeaders =
      env->GetMethodID(jcls_JvmCallbackContext, method, "(JZ[J)Ljava/lang/Object;");

  jlongArray j_stream_intel = native_stream_intel_to_array(env, stream_intel);
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobject result = env->CallObjectMethod(j_context, jmid_onHeaders, (jlong)headers.length,
                                         end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel);

  env->DeleteLocalRef(j_stream_intel);
  env->DeleteLocalRef(jcls_JvmCallbackContext);

  return result;
}

static void* jvm_on_response_headers(envoy_headers headers, bool end_stream,
                                     envoy_stream_intel stream_intel, void* context) {
  return jvm_on_headers("onResponseHeaders", headers, end_stream, stream_intel, context);
}

static envoy_filter_headers_status
jvm_http_filter_on_request_headers(envoy_headers headers, bool end_stream, const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(jvm_on_headers(
      "onRequestHeaders", headers, end_stream, envoy_stream_intel{}, const_cast<void*>(context)));

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
  jobjectArray result = static_cast<jobjectArray>(jvm_on_headers(
      "onResponseHeaders", headers, end_stream, envoy_stream_intel{}, const_cast<void*>(context)));

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

static void* jvm_on_data(const char* method, envoy_data data, bool end_stream,
                         envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_data");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onData =
      env->GetMethodID(jcls_JvmCallbackContext, method, "([BZ[J)Ljava/lang/Object;");

  jbyteArray j_data = native_data_to_array(env, data);
  jlongArray j_stream_intel = native_stream_intel_to_array(env, stream_intel);
  jobject result = env->CallObjectMethod(j_context, jmid_onData, j_data,
                                         end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel);

  env->DeleteLocalRef(j_stream_intel);
  env->DeleteLocalRef(j_data);
  env->DeleteLocalRef(jcls_JvmCallbackContext);
  release_envoy_data(data);

  return result;
}

static void* jvm_on_response_data(envoy_data data, bool end_stream, envoy_stream_intel stream_intel,
                                  void* context) {
  return jvm_on_data("onResponseData", data, end_stream, stream_intel, context);
}

static envoy_filter_data_status jvm_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(jvm_on_data(
      "onRequestData", data, end_stream, envoy_stream_intel{}, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobject j_data = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_data native_data = buffer_to_native_data(env, j_data);

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 2));
    pending_headers = to_native_headers_ptr(env, j_headers);
    env->DeleteLocalRef(j_headers);
  }

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_data);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static envoy_filter_data_status jvm_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(jvm_on_data(
      "onResponseData", data, end_stream, envoy_stream_intel{}, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobject j_data = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_data native_data = buffer_to_native_data(env, j_data);

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 2));
    pending_headers = to_native_headers_ptr(env, j_headers);
    env->DeleteLocalRef(j_headers);
  }

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_data);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static void* jvm_on_metadata(envoy_headers metadata, envoy_stream_intel stream_intel,
                             void* context) {
  jni_log("[Envoy]", "jvm_on_metadata");
  jni_log("[Envoy]", std::to_string(metadata.length).c_str());
  return NULL;
}

static void* jvm_on_trailers(const char* method, envoy_headers trailers,
                             envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_trailers");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);
  pass_headers("passHeader", trailers, j_context);

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onTrailers =
      env->GetMethodID(jcls_JvmCallbackContext, method, "(J[J)Ljava/lang/Object;");

  jlongArray j_stream_intel = native_stream_intel_to_array(env, stream_intel);
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobject result =
      env->CallObjectMethod(j_context, jmid_onTrailers, (jlong)trailers.length, j_stream_intel);

  env->DeleteLocalRef(j_stream_intel);
  env->DeleteLocalRef(jcls_JvmCallbackContext);

  return result;
}

static void* jvm_on_response_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                      void* context) {
  return jvm_on_trailers("onResponseTrailers", trailers, stream_intel, context);
}

static envoy_filter_trailers_status jvm_http_filter_on_request_trailers(envoy_headers trailers,
                                                                        const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(jvm_on_trailers(
      "onRequestTrailers", trailers, envoy_stream_intel{}, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_trailers = to_native_headers(env, j_trailers);

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 2));
    pending_headers = to_native_headers_ptr(env, j_headers);
    env->DeleteLocalRef(j_headers);

    jobject j_data = static_cast<jobject>(env->GetObjectArrayElement(result, 3));
    pending_data = buffer_to_native_data_ptr(env, j_data);
    env->DeleteLocalRef(j_data);
  }

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_trailers);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static envoy_filter_trailers_status jvm_http_filter_on_response_trailers(envoy_headers trailers,
                                                                         const void* context) {
  JNIEnv* env = get_env();
  jobjectArray result = static_cast<jobjectArray>(jvm_on_trailers(
      "onResponseTrailers", trailers, envoy_stream_intel{}, const_cast<void*>(context)));

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));

  int unboxed_status = unbox_integer(env, status);
  envoy_headers native_trailers = to_native_headers(env, j_trailers);

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 2));
    pending_headers = to_native_headers_ptr(env, j_headers);
    env->DeleteLocalRef(j_headers);

    jobject j_data = static_cast<jobject>(env->GetObjectArrayElement(result, 3));
    pending_data = buffer_to_native_data_ptr(env, j_data);
    env->DeleteLocalRef(j_data);
  }

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_trailers);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static void jvm_http_filter_set_request_callbacks(envoy_http_filter_callbacks callbacks,
                                                  const void* context) {

  jni_log("[Envoy]", "jvm_http_filter_set_request_callbacks");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);

  envoy_http_filter_callbacks* on_heap_callbacks =
      static_cast<envoy_http_filter_callbacks*>(safe_malloc(sizeof(envoy_http_filter_callbacks)));
  *on_heap_callbacks = callbacks;
  jlong callback_handle = reinterpret_cast<jlong>(on_heap_callbacks);

  jmethodID jmid_setRequestFilterCallbacks =
      env->GetMethodID(jcls_JvmCallbackContext, "setRequestFilterCallbacks", "(J)V");
  env->CallVoidMethod(j_context, jmid_setRequestFilterCallbacks, callback_handle);

  env->DeleteLocalRef(jcls_JvmCallbackContext);
}

static void jvm_http_filter_set_response_callbacks(envoy_http_filter_callbacks callbacks,
                                                   const void* context) {

  jni_log("[Envoy]", "jvm_http_filter_set_response_callbacks");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);

  envoy_http_filter_callbacks* on_heap_callbacks =
      static_cast<envoy_http_filter_callbacks*>(safe_malloc(sizeof(envoy_http_filter_callbacks)));
  *on_heap_callbacks = callbacks;
  jlong callback_handle = reinterpret_cast<jlong>(on_heap_callbacks);

  jmethodID jmid_setResponseFilterCallbacks =
      env->GetMethodID(jcls_JvmCallbackContext, "setResponseFilterCallbacks", "(J)V");
  env->CallVoidMethod(j_context, jmid_setResponseFilterCallbacks, callback_handle);

  env->DeleteLocalRef(jcls_JvmCallbackContext);
}

static envoy_filter_resume_status
jvm_http_filter_on_resume(const char* method, envoy_headers* headers, envoy_data* data,
                          envoy_headers* trailers, bool end_stream, const void* context) {
  jni_log("[Envoy]", "jvm_on_resume");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jlong headers_length = -1;
  if (headers) {
    headers_length = (jlong)headers->length;
    pass_headers("passHeader", *headers, j_context);
  }
  jbyteArray j_in_data = NULL;
  if (data) {
    j_in_data = native_data_to_array(env, *data);
  }
  jlong trailers_length = -1;
  if (trailers) {
    trailers_length = (jlong)trailers->length;
    pass_headers("passTrailer", *trailers, j_context);
  }

  jclass jcls_JvmCallbackContext = env->GetObjectClass(j_context);
  jmethodID jmid_onResume =
      env->GetMethodID(jcls_JvmCallbackContext, method, "(J[BJZ)Ljava/lang/Object;");
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobjectArray result = static_cast<jobjectArray>(
      env->CallObjectMethod(j_context, jmid_onResume, headers_length, j_in_data, trailers_length,
                            end_stream ? JNI_TRUE : JNI_FALSE));

  env->DeleteLocalRef(jcls_JvmCallbackContext);
  if (j_in_data != NULL) {
    env->DeleteLocalRef(j_in_data);
  }

  jobject status = env->GetObjectArrayElement(result, 0);
  jobjectArray j_headers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 1));
  jobject j_data = static_cast<jobject>(env->GetObjectArrayElement(result, 2));
  jobjectArray j_trailers = static_cast<jobjectArray>(env->GetObjectArrayElement(result, 3));

  int unboxed_status = unbox_integer(env, status);
  envoy_headers* pending_headers = to_native_headers_ptr(env, j_headers);
  envoy_data* pending_data = buffer_to_native_data_ptr(env, j_data);
  envoy_headers* pending_trailers = to_native_headers_ptr(env, j_trailers);

  env->DeleteLocalRef(result);
  env->DeleteLocalRef(status);
  env->DeleteLocalRef(j_headers);
  env->DeleteLocalRef(j_data);
  env->DeleteLocalRef(j_trailers);

  return (envoy_filter_resume_status){/*status*/ unboxed_status,
                                      /*pending_headers*/ pending_headers,
                                      /*pending_data*/ pending_data,
                                      /*pending_trailers*/ pending_trailers};
}

static envoy_filter_resume_status
jvm_http_filter_on_resume_request(envoy_headers* headers, envoy_data* data, envoy_headers* trailers,
                                  bool end_stream, const void* context) {
  return jvm_http_filter_on_resume("onResumeRequest", headers, data, trailers, end_stream, context);
}

static envoy_filter_resume_status
jvm_http_filter_on_resume_response(envoy_headers* headers, envoy_data* data,
                                   envoy_headers* trailers, bool end_stream, const void* context) {
  return jvm_http_filter_on_resume("onResumeResponse", headers, data, trailers, end_stream,
                                   context);
}

static void* jvm_on_complete(envoy_stream_intel, void* context) {
  jni_delete_global_ref(context);
  return NULL;
}

static void* call_jvm_on_error(envoy_error error, envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_error");
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmObserverContext = env->GetObjectClass(j_context);
  jmethodID jmid_onError =
      env->GetMethodID(jcls_JvmObserverContext, "onError", "(I[BI[J)Ljava/lang/Object;");

  jbyteArray j_error_message = native_data_to_array(env, error.message);
  jlongArray j_stream_intel = native_stream_intel_to_array(env, stream_intel);

  jobject result = env->CallObjectMethod(j_context, jmid_onError, error.error_code, j_error_message,
                                         error.attempt_count, j_stream_intel);

  env->DeleteLocalRef(j_stream_intel);
  env->DeleteLocalRef(j_error_message);
  env->DeleteLocalRef(jcls_JvmObserverContext);
  release_envoy_error(error);
  return result;
}

static void* jvm_on_error(envoy_error error, envoy_stream_intel stream_intel, void* context) {
  void* result = call_jvm_on_error(error, stream_intel, context);
  jni_delete_global_ref(context);
  return result;
}

static void* call_jvm_on_cancel(envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_cancel");

  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(context);

  jclass jcls_JvmObserverContext = env->GetObjectClass(j_context);
  jmethodID jmid_onCancel =
      env->GetMethodID(jcls_JvmObserverContext, "onCancel", "([J)Ljava/lang/Object;");

  jlongArray j_stream_intel = native_stream_intel_to_array(env, stream_intel);

  jobject result = env->CallObjectMethod(j_context, jmid_onCancel, j_stream_intel);

  env->DeleteLocalRef(j_stream_intel);
  env->DeleteLocalRef(jcls_JvmObserverContext);
  return result;
}

static void* jvm_on_cancel(envoy_stream_intel stream_intel, void* context) {
  void* result = call_jvm_on_cancel(stream_intel, context);
  jni_delete_global_ref(context);
  return result;
}

static void jvm_http_filter_on_error(envoy_error error, const void* context) {
  call_jvm_on_error(error, envoy_stream_intel{}, const_cast<void*>(context));
}

static void jvm_http_filter_on_cancel(const void* context) {
  call_jvm_on_cancel(envoy_stream_intel{}, const_cast<void*>(context));
}

// JvmFilterFactoryContext

static const void* jvm_http_filter_init(const void* context) {
  jni_log("[Envoy]", "jvm_filter_init");

  JNIEnv* env = get_env();

  envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
  jobject j_context = static_cast<jobject>(const_cast<void*>(c_filter->static_context));

  jni_log_fmt("[Envoy]", "j_context: %p", j_context);

  jclass jcls_JvmFilterFactoryContext = env->GetObjectClass(j_context);
  jmethodID jmid_create = env->GetMethodID(jcls_JvmFilterFactoryContext, "create",
                                           "()Lio/envoyproxy/envoymobile/engine/JvmFilterContext;");

  jobject j_filter = env->CallObjectMethod(j_context, jmid_create);
  jni_log_fmt("[Envoy]", "j_filter: %p", j_filter);
  jobject retained_filter = env->NewGlobalRef(j_filter);

  env->DeleteLocalRef(jcls_JvmFilterFactoryContext);
  env->DeleteLocalRef(j_filter);

  return retained_filter;
}

// EnvoyStringAccessor

static envoy_data jvm_get_string(const void* context) {
  JNIEnv* env = get_env();
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jclass jcls_JvmStringAccessorContext = env->GetObjectClass(j_context);
  jmethodID jmid_getString =
      env->GetMethodID(jcls_JvmStringAccessorContext, "getEnvoyString", "()[B");
  jbyteArray j_data = (jbyteArray)env->CallObjectMethod(j_context, jmid_getString);
  envoy_data native_data = array_to_native_data(env, j_data);

  env->DeleteLocalRef(jcls_JvmStringAccessorContext);
  env->DeleteLocalRef(j_data);

  return native_data;
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initStream(
    JNIEnv* env, jclass, jlong engine_handle) {

  return init_stream(static_cast<envoy_engine_t>(engine_handle));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_startStream(
    JNIEnv* env, jclass, jlong stream_handle, jobject j_context, jboolean explicit_flow_control) {

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
  envoy_status_t result = start_stream(static_cast<envoy_stream_t>(stream_handle), native_callbacks,
                                       explicit_flow_control);
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
  jni_log("[Envoy]", "registerFilterFactory");
  jni_log_fmt("[Envoy]", "j_context: %p", j_context);
  jclass jcls_JvmFilterFactoryContext = env->GetObjectClass(j_context);
  jobject retained_context = env->NewGlobalRef(j_context);
  jni_log_fmt("[Envoy]", "retained_context: %p", retained_context);
  envoy_http_filter* api = (envoy_http_filter*)safe_malloc(sizeof(envoy_http_filter));
  api->init_filter = jvm_http_filter_init;
  api->on_request_headers = jvm_http_filter_on_request_headers;
  api->on_request_data = jvm_http_filter_on_request_data;
  api->on_request_trailers = jvm_http_filter_on_request_trailers;
  api->on_response_headers = jvm_http_filter_on_response_headers;
  api->on_response_data = jvm_http_filter_on_response_data;
  api->on_response_trailers = jvm_http_filter_on_response_trailers;
  api->set_request_callbacks = jvm_http_filter_set_request_callbacks;
  api->on_resume_request = jvm_http_filter_on_resume_request;
  api->set_response_callbacks = jvm_http_filter_set_response_callbacks;
  api->on_resume_response = jvm_http_filter_on_resume_response;
  api->on_cancel = jvm_http_filter_on_cancel;
  api->on_error = jvm_http_filter_on_error;
  api->release_filter = jni_delete_const_global_ref;
  api->static_context = retained_context;
  api->instance_context = NULL;

  envoy_status_t result = register_platform_api(env->GetStringUTFChars(filter_name, nullptr), api);
  env->DeleteLocalRef(jcls_JvmFilterFactoryContext);
  return result;
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_EnvoyHTTPFilterCallbacksImpl_callResumeIteration(
    JNIEnv* env, jclass, jlong callback_handle, jobject j_context) {
  jni_log("[Envoy]", "callResumeIteration");
  // Context is only passed here to ensure it's not inadvertently gc'd during execution of this
  // function. To be extra safe, do an explicit retain with a GlobalRef.
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_filter_callbacks* callbacks =
      reinterpret_cast<envoy_http_filter_callbacks*>(callback_handle);
  callbacks->resume_iteration(callbacks->callback_context);
  env->DeleteGlobalRef(retained_context);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_EnvoyHTTPFilterCallbacksImpl_callResetIdleTimer(
    JNIEnv* env, jclass, jlong callback_handle, jobject j_context) {
  jni_log("[Envoy]", "callResetIdleTimer");
  // Context is only passed here to ensure it's not inadvertently gc'd during execution of this
  // function. To be extra safe, do an explicit retain with a GlobalRef.
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_filter_callbacks* callbacks =
      reinterpret_cast<envoy_http_filter_callbacks*>(callback_handle);
  callbacks->reset_idle(callbacks->callback_context);
  env->DeleteGlobalRef(retained_context);
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_EnvoyHTTPFilterCallbacksImpl_callReleaseCallbacks(
    JNIEnv* env, jclass, jlong callback_handle) {
  jni_log("[Envoy]", "callReleaseCallbacks");
  envoy_http_filter_callbacks* callbacks =
      reinterpret_cast<envoy_http_filter_callbacks*>(callback_handle);
  callbacks->release_callbacks(callbacks->callback_context);
  free(callbacks);
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_readData(
    JNIEnv* env, jclass, jlong stream_handle, jlong byte_count) {

  return read_data(static_cast<envoy_stream_t>(stream_handle), byte_count);
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
  if (end_stream) {
    jni_log("[Envoy]", "jvm_send_data_end_stream");
  }

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
  jni_log("[Envoy]", "jvm_send_trailers");
  return send_trailers(static_cast<envoy_stream_t>(stream_handle),
                       to_native_headers(env, trailers));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetStream(
    JNIEnv* env, jclass, jlong stream_handle) {

  return reset_stream(static_cast<envoy_stream_t>(stream_handle));
}

// EnvoyStringAccessor

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerStringAccessor(JNIEnv* env, jclass,
                                                                        jstring accessor_name,
                                                                        jobject j_context) {

  // TODO(goaway): The retained_context leaks, but it's tied to the life of the engine.
  // This will need to be updated for https://github.com/lyft/envoy-mobile/issues/332.
  jclass jcls_JvmStringAccessorContext = env->GetObjectClass(j_context);
  jobject retained_context = env->NewGlobalRef(j_context);

  envoy_string_accessor* string_accessor =
      (envoy_string_accessor*)safe_malloc(sizeof(envoy_string_accessor));
  string_accessor->get_string = jvm_get_string;
  string_accessor->context = retained_context;

  envoy_status_t result =
      register_platform_api(env->GetStringUTFChars(accessor_name, nullptr), string_accessor);
  env->DeleteLocalRef(jcls_JvmStringAccessorContext);
  return result;
}

static void jvm_on_track(envoy_map events, const void* context) {
  jni_log("[Envoy]", "jvm_on_track");
  if (context == nullptr) {
    return;
  }

  JNIEnv* env = get_env();
  jobject events_hashmap = native_map_to_map(env, events);

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jclass jcls_EnvoyEventTracker = env->GetObjectClass(j_context);
  jmethodID jmid_onTrack = env->GetMethodID(jcls_EnvoyEventTracker, "track", "(Ljava/util/Map;)V");
  env->CallVoidMethod(j_context, jmid_onTrack, events_hashmap);

  release_envoy_map(events);
  env->DeleteLocalRef(events_hashmap);
  env->DeleteLocalRef(jcls_EnvoyEventTracker);
}

// EnvoyEventTracker

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerEventTracker(JNIEnv* env, jclass,
                                                                      jobject j_event_tracker) {
  jni_log("[Envoy]", "register event tracker");

  // TODO(goaway): The retained_context leaks, but it's tied to the life of the engine.
  // This will need to be updated for https://github.com/lyft/envoy-mobile/issues/332.
  jobject retained_context = env->NewGlobalRef(j_event_tracker);
  jni_log_fmt("[Envoy]", "retained_context: %p", retained_context);
  envoy_event_tracker* event_tracker = nullptr;
  if (j_event_tracker != nullptr) {
    event_tracker = (envoy_event_tracker*)safe_malloc(sizeof(envoy_event_tracker));
    event_tracker->track = jvm_on_track;
    event_tracker->context = retained_context;
  }
  envoy_status_t result = register_platform_api(envoy_event_tracker_api_name, event_tracker);
  return result;
}
