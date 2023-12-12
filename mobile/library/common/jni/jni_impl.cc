#include <cstddef>
#include <string>
#include <utility>

#include "source/common/protobuf/protobuf.h"

#include "library/cc/engine_builder.h"
#include "library/common/api/c_types.h"
#include "library/common/data/utility.h"
#include "library/common/engine.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/extensions/key_value/platform/c_types.h"
#include "library/common/jni/android_network_utility.h"
#include "library/common/jni/import/jni_import.h"
#include "library/common/jni/jni_support.h"
#include "library/common/jni/jni_utility.h"
#include "library/common/jni/types/exception.h"
#include "library/common/jni/types/java_virtual_machine.h"
#include "library/common/main_interface.h"
#include "library/common/types/managed_envoy_headers.h"

using Envoy::Platform::EngineBuilder;

// NOLINT(namespace-envoy)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  const auto result = Envoy::JNI::JavaVirtualMachine::initialize(vm);
  if (result != JNI_OK) {
    return result;
  }

  return Envoy::JNI::JavaVirtualMachine::getJNIVersion();
}

// JniLibrary

static void jvm_on_engine_running(void* context) {
  if (context == nullptr) {
    return;
  }

  jni_log("[Envoy]", "jvm_on_engine_running");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmonEngineRunningContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onEngineRunning = jni_helper.getMethodId(
      jcls_JvmonEngineRunningContext.get(), "invokeOnEngineRunning", "()Ljava/lang/Object;");
  Envoy::JNI::LocalRefUniquePtr<jobject> unused =
      jni_helper.callObjectMethod(j_context, jmid_onEngineRunning);

  // TODO(goaway): This isn't re-used by other engine callbacks, so it's safe to delete here.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  jni_helper.getEnv()->DeleteGlobalRef(j_context);
}

static void jvm_on_log(envoy_data data, const void* context) {
  if (context == nullptr) {
    return;
  }

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  Envoy::JNI::LocalRefUniquePtr<jstring> str = Envoy::JNI::envoyDataToJavaString(jni_helper, data);

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmLoggerContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onLog =
      jni_helper.getMethodId(jcls_JvmLoggerContext.get(), "log", "(Ljava/lang/String;)V");
  jni_helper.callVoidMethod(j_context, jmid_onLog, str.get());

  release_envoy_data(data);
}

static void jvm_on_exit(void*) {
  jni_log("[Envoy]", "library is exiting");
  // Note that this is not dispatched because the thread that
  // needs to be detached is the engine thread.
  // This function is called from the context of the engine's
  // thread due to it being posted to the engine's event dispatcher.
  Envoy::JNI::JavaVirtualMachine::detachCurrentThread();
}

static void jvm_on_track(envoy_map events, const void* context) {
  jni_log("[Envoy]", "jvm_on_track");
  if (context == nullptr) {
    return;
  }

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  Envoy::JNI::LocalRefUniquePtr<jobject> events_hashmap =
      Envoy::JNI::envoyMapToJavaMap(jni_helper, events);

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_EnvoyEventTracker =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onTrack =
      jni_helper.getMethodId(jcls_EnvoyEventTracker.get(), "track", "(Ljava/util/Map;)V");
  jni_helper.callVoidMethod(j_context, jmid_onTrack, events_hashmap.get());

  release_envoy_map(events);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_setLogLevel(JNIEnv* env, jclass, jint level) {
  Envoy::Logger::Context::changeAllLogLevels(static_cast<spdlog::level::level_enum>(level));
  return 0;
}

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env, jclass, jobject on_start_context, jobject envoy_logger_context,
    jobject j_event_tracker) {
  jobject retained_on_start_context =
      env->NewGlobalRef(on_start_context); // Required to keep context in memory
  envoy_engine_callbacks native_callbacks = {jvm_on_engine_running, jvm_on_exit,
                                             retained_on_start_context};

  const jobject retained_logger_context = env->NewGlobalRef(envoy_logger_context);
  envoy_logger logger = {nullptr, nullptr, nullptr};
  if (envoy_logger_context != nullptr) {
    logger = envoy_logger{jvm_on_log, Envoy::JNI::jniDeleteConstGlobalRef, retained_logger_context};
  }

  envoy_event_tracker event_tracker = {nullptr, nullptr};
  if (j_event_tracker != nullptr) {
    // TODO(goaway): The retained_context leaks, but it's tied to the life of the engine.
    // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332.
    jobject retained_context = env->NewGlobalRef(j_event_tracker);
    jni_log_fmt("[Envoy]", "retained_context: %p", retained_context);
    event_tracker.track = jvm_on_track;
    event_tracker.context = retained_context;
  }

  return init_engine(native_callbacks, logger, event_tracker);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(
    JNIEnv* env, jclass, jlong engine, jstring config, jlong bootstrap_ptr, jstring log_level) {
  const char* string_config = env->GetStringUTFChars(config, nullptr);
  const char* string_log_level = env->GetStringUTFChars(log_level, nullptr);
  // This should be either 0 (null) or a pointer generated by createBootstrap.
  // As documented in JniLibrary.java, take ownership.
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));

  jint result;
  if (!bootstrap) {
    result = run_engine(engine, string_config, string_log_level);
  } else {
    auto options = std::make_unique<Envoy::OptionsImplBase>();
    options->setConfigProto(std::move(bootstrap));
    ENVOY_BUG(options->setLogLevel(string_log_level).ok(), "invalid log level");
    options->setConcurrency(1);
    result = reinterpret_cast<Envoy::Engine*>(engine)->run(std::move(options));
  }

  env->ReleaseStringUTFChars(config, string_config);
  env->ReleaseStringUTFChars(log_level, string_log_level);
  return result;
}

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_terminateEngine(
    JNIEnv* env, jclass, jlong engine_handle) {
  terminate_engine(static_cast<envoy_engine_t>(engine_handle), /* release */ false);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordCounterInc(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring elements, jobjectArray tags, jint count) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr native_elements = jni_helper.getStringUtfChars(elements, nullptr);
  jint result = record_counter_inc(
      engine, native_elements.get(),
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyStatsTags(jni_helper, tags), count);
  return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_dumpStats(JNIEnv* env,
                                                           jclass, // class
                                                           jlong engine) {
  jni_log("[Envoy]", "dumpStats");
  envoy_data data;
  jint result = dump_stats(engine, &data);
  if (result != ENVOY_SUCCESS) {
    return env->NewStringUTF("");
  }

  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::LocalRefUniquePtr<jstring> str = Envoy::JNI::envoyDataToJavaString(jni_helper, data);
  release_envoy_data(data);

  return str.release();
}

// JvmCallbackContext

static void passHeaders(const char* method, const Envoy::Types::ManagedEnvoyHeaders& headers,
                        jobject j_context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_passHeader =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method, "([B[BZ)V");
  jboolean start_headers = JNI_TRUE;

  for (envoy_map_size_t i = 0; i < headers.get().length; i++) {
    // Note: this is just an initial implementation, and we will pass a more optimized structure in
    // the future.
    // Note: the JNI function NewStringUTF would appear to be an appealing option here, except it
    // requires a null-terminated *modified* UTF-8 string.

    // Create platform byte array for header key
    Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_key =
        Envoy::JNI::envoyDataToJavaByteArray(jni_helper, headers.get().entries[i].key);
    // Create platform byte array for header value
    Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_value =
        Envoy::JNI::envoyDataToJavaByteArray(jni_helper, headers.get().entries[i].value);

    // Pass this header pair to the platform
    jni_helper.callVoidMethod(j_context, jmid_passHeader, j_key.get(), j_value.get(),
                              start_headers);

    // We don't release local refs currently because we've pushed a large enough frame, but we could
    // consider this and/or periodically popping the frame.
    start_headers = JNI_FALSE;
  }
}

// Platform callback implementation
// These methods call jvm methods which means the local references created will not be
// released automatically. Manual bookkeeping is required for these methods.

static void* jvm_on_headers(const char* method, const Envoy::Types::ManagedEnvoyHeaders& headers,
                            bool end_stream, envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_headers");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);
  passHeaders("passHeader", headers, j_context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onHeaders =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method, "(JZ[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  Envoy::JNI::LocalRefUniquePtr<jobject> result =
      jni_helper.callObjectMethod(j_context, jmid_onHeaders, (jlong)headers.get().length,
                                  end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel.get());
  // TODO(Augustyniak): Pass the name of the filter in here so that we can instrument the origin of
  // the JNI exception better.
  bool exception_cleared = Envoy::JNI::Exception::checkAndClear(method);

  if (!exception_cleared) {
    return result.release();
  }

  // Create a "no operation" result:
  //  1. Tell the filter chain to continue the iteration.
  //  2. Return headers received on as method's input as part of the method's output.
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_object_array =
      jni_helper.findClass("java/lang/Object");
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> noopResult =
      jni_helper.newObjectArray(2, jcls_object_array.get(), NULL);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_int = jni_helper.findClass("java/lang/Integer");
  jmethodID jmid_intInit = jni_helper.getMethodId(jcls_int.get(), "<init>", "(I)V");
  Envoy::JNI::LocalRefUniquePtr<jobject> j_status =
      jni_helper.newObject(jcls_int.get(), jmid_intInit, 0);
  // Set status to "0" (FilterHeadersStatus::Continue). Signal that the intent
  // is to continue the iteration of the filter chain.
  jni_helper.setObjectArrayElement(noopResult.get(), 0, j_status.get());

  // Since the "on headers" call threw an exception set input headers as output headers.
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      Envoy::JNI::envoyHeadersToJavaArrayOfObjectArray(jni_helper, headers);
  jni_helper.setObjectArrayElement(noopResult.get(), 1, j_headers.get());

  return noopResult.release();
}

static void* jvm_on_response_headers(envoy_headers headers, bool end_stream,
                                     envoy_stream_intel stream_intel, void* context) {
  const auto managed_headers = Envoy::Types::ManagedEnvoyHeaders(headers);
  return jvm_on_headers("onResponseHeaders", managed_headers, end_stream, stream_intel, context);
}

static envoy_filter_headers_status
jvm_http_filter_on_request_headers(envoy_headers input_headers, bool end_stream,
                                   envoy_stream_intel stream_intel, const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  const auto headers = Envoy::Types::ManagedEnvoyHeaders(input_headers);
  jobjectArray result = static_cast<jobjectArray>(jvm_on_headers(
      "onRequestHeaders", headers, end_stream, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                         /*headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_headers native_headers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_headers.get());

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
}

static envoy_filter_headers_status
jvm_http_filter_on_response_headers(envoy_headers input_headers, bool end_stream,
                                    envoy_stream_intel stream_intel, const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  const auto headers = Envoy::Types::ManagedEnvoyHeaders(input_headers);
  jobjectArray result = static_cast<jobjectArray>(jvm_on_headers(
      "onResponseHeaders", headers, end_stream, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                         /*headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_headers native_headers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_headers.get());

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
}

static void* jvm_on_data(const char* method, envoy_data data, bool end_stream,
                         envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_data");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onData =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method, "([BZ[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_data =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, data);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  jobject result = jni_helper
                       .callObjectMethod(j_context, jmid_onData, j_data.get(),
                                         end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel.get())
                       .release();

  release_envoy_data(data);

  return result;
}

static void* jvm_on_response_data(envoy_data data, bool end_stream, envoy_stream_intel stream_intel,
                                  void* context) {
  return jvm_on_data("onResponseData", data, end_stream, stream_intel, context);
}

static envoy_filter_data_status jvm_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                envoy_stream_intel stream_intel,
                                                                const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_data("onRequestData", data, end_stream, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                      /*data*/ {},
                                      /*pending_headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_data =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_data native_data = Envoy::JNI::javaByteBufferToEnvoyData(jni_helper, j_data.get());

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result, 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());
  }

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static envoy_filter_data_status jvm_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 envoy_stream_intel stream_intel,
                                                                 const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_data("onResponseData", data, end_stream, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                      /*data*/ {},
                                      /*pending_headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_data =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_data native_data = Envoy::JNI::javaByteBufferToEnvoyData(jni_helper, j_data.get());

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result, 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());
  }

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static void* jvm_on_metadata(envoy_headers metadata, envoy_stream_intel stream_intel,
                             void* context) {
  jni_log("[Envoy]", "jvm_on_metadata");
  jni_log("[Envoy]", std::to_string(metadata.length).c_str());
  return nullptr;
}

static void* jvm_on_trailers(const char* method, envoy_headers trailers,
                             envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_trailers");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);
  passHeaders("passHeader", trailers, j_context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onTrailers =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method, "(J[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  jobject result = jni_helper
                       .callObjectMethod(j_context, jmid_onTrailers, (jlong)trailers.length,
                                         j_stream_intel.get())
                       .release();

  return result;
}

static void* jvm_on_response_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                      void* context) {
  return jvm_on_trailers("onResponseTrailers", trailers, stream_intel, context);
}

static envoy_filter_trailers_status
jvm_http_filter_on_request_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                    const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_trailers("onRequestTrailers", trailers, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                          /*trailers*/ {},
                                          /*pending_headers*/ {},
                                          /*pending_data*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_trailers =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_headers native_trailers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_trailers.get());

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result, 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());

    Envoy::JNI::LocalRefUniquePtr<jobject> j_data = jni_helper.getObjectArrayElement(result, 3);
    pending_data = Envoy::JNI::javaByteBufferToEnvoyDataPtr(jni_helper, j_data.get());
  }

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static envoy_filter_trailers_status
jvm_http_filter_on_response_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                     const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobjectArray result = static_cast<jobjectArray>(
      jvm_on_trailers("onResponseTrailers", trailers, stream_intel, const_cast<void*>(context)));

  if (result == NULL || jni_helper.getArrayLength(result) < 2) {
    jni_helper.getEnv()->DeleteLocalRef(result);
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                          /*trailers*/ {},
                                          /*pending_headers*/ {},
                                          /*pending_data*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result, 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_trailers =
      jni_helper.getObjectArrayElement<jobjectArray>(result, 1);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_headers native_trailers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_trailers.get());

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result, 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());

    Envoy::JNI::LocalRefUniquePtr<jobject> j_data = jni_helper.getObjectArrayElement(result, 3);
    pending_data = Envoy::JNI::javaByteBufferToEnvoyDataPtr(jni_helper, j_data.get());
  }

  jni_helper.getEnv()->DeleteLocalRef(result);

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static void jvm_http_filter_set_request_callbacks(envoy_http_filter_callbacks callbacks,
                                                  const void* context) {

  jni_log("[Envoy]", "jvm_http_filter_set_request_callbacks");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);

  envoy_http_filter_callbacks* on_heap_callbacks =
      static_cast<envoy_http_filter_callbacks*>(safe_malloc(sizeof(envoy_http_filter_callbacks)));
  *on_heap_callbacks = callbacks;
  jlong callback_handle = reinterpret_cast<jlong>(on_heap_callbacks);

  jmethodID jmid_setRequestFilterCallbacks =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), "setRequestFilterCallbacks", "(J)V");
  jni_helper.callVoidMethod(j_context, jmid_setRequestFilterCallbacks, callback_handle);
}

static void jvm_http_filter_set_response_callbacks(envoy_http_filter_callbacks callbacks,
                                                   const void* context) {

  jni_log("[Envoy]", "jvm_http_filter_set_response_callbacks");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);

  envoy_http_filter_callbacks* on_heap_callbacks =
      static_cast<envoy_http_filter_callbacks*>(safe_malloc(sizeof(envoy_http_filter_callbacks)));
  *on_heap_callbacks = callbacks;
  jlong callback_handle = reinterpret_cast<jlong>(on_heap_callbacks);

  jmethodID jmid_setResponseFilterCallbacks =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), "setResponseFilterCallbacks", "(J)V");
  jni_helper.callVoidMethod(j_context, jmid_setResponseFilterCallbacks, callback_handle);
}

static envoy_filter_resume_status
jvm_http_filter_on_resume(const char* method, envoy_headers* headers, envoy_data* data,
                          envoy_headers* trailers, bool end_stream, envoy_stream_intel stream_intel,
                          const void* context) {
  jni_log("[Envoy]", "jvm_on_resume");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jlong headers_length = -1;
  if (headers) {
    headers_length = (jlong)headers->length;
    passHeaders("passHeader", *headers, j_context);
  }
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_in_data = Envoy::JNI::LocalRefUniquePtr<jbyteArray>(
      nullptr, Envoy::JNI::LocalRefDeleter(jni_helper.getEnv()));
  if (data) {
    j_in_data = Envoy::JNI::envoyDataToJavaByteArray(jni_helper, *data);
  }
  jlong trailers_length = -1;
  if (trailers) {
    trailers_length = (jlong)trailers->length;
    passHeaders("passTrailer", *trailers, j_context);
  }
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onResume =
      jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method, "(J[BJZ[J)Ljava/lang/Object;");
  // Note: be careful of JVM types. Before we casted to jlong we were getting integer problems.
  // TODO: make this cast safer.
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jni_helper.callObjectMethod<jobjectArray>(
      j_context, jmid_onResume, headers_length, j_in_data.get(), trailers_length,
      end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel.get());

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);
  Envoy::JNI::LocalRefUniquePtr<jobject> j_data = jni_helper.getObjectArrayElement(result.get(), 2);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_trailers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 3);

  int unboxed_status = Envoy::JNI::javaIntegerTotInt(jni_helper, status.get());
  envoy_headers* pending_headers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());
  envoy_data* pending_data = Envoy::JNI::javaByteBufferToEnvoyDataPtr(jni_helper, j_data.get());
  envoy_headers* pending_trailers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_trailers.get());

  return (envoy_filter_resume_status){/*status*/ unboxed_status,
                                      /*pending_headers*/ pending_headers,
                                      /*pending_data*/ pending_data,
                                      /*pending_trailers*/ pending_trailers};
}

static envoy_filter_resume_status
jvm_http_filter_on_resume_request(envoy_headers* headers, envoy_data* data, envoy_headers* trailers,
                                  bool end_stream, envoy_stream_intel stream_intel,
                                  const void* context) {
  return jvm_http_filter_on_resume("onResumeRequest", headers, data, trailers, end_stream,
                                   stream_intel, context);
}

static envoy_filter_resume_status
jvm_http_filter_on_resume_response(envoy_headers* headers, envoy_data* data,
                                   envoy_headers* trailers, bool end_stream,
                                   envoy_stream_intel stream_intel, const void* context) {
  return jvm_http_filter_on_resume("onResumeResponse", headers, data, trailers, end_stream,
                                   stream_intel, context);
}

static void* call_jvm_on_complete(envoy_stream_intel stream_intel,
                                  envoy_final_stream_intel final_stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_complete");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmObserverContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onComplete = jni_helper.getMethodId(jcls_JvmObserverContext.get(), "onComplete",
                                                     "([J[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_final_stream_intel =
      Envoy::JNI::envoyFinalStreamIntelToJavaLongArray(jni_helper, final_stream_intel);
  jobject result = jni_helper
                       .callObjectMethod(j_context, jmid_onComplete, j_stream_intel.get(),
                                         j_final_stream_intel.get())
                       .release();

  return result;
}

static void* call_jvm_on_error(envoy_error error, envoy_stream_intel stream_intel,
                               envoy_final_stream_intel final_stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_error");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmObserverContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onError = jni_helper.getMethodId(jcls_JvmObserverContext.get(), "onError",
                                                  "(I[BI[J[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_error_message =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, error.message);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_final_stream_intel =
      Envoy::JNI::envoyFinalStreamIntelToJavaLongArray(jni_helper, final_stream_intel);

  jobject result =
      jni_helper
          .callObjectMethod(j_context, jmid_onError, error.error_code, j_error_message.get(),
                            error.attempt_count, j_stream_intel.get(), j_final_stream_intel.get())
          .release();

  release_envoy_error(error);
  return result;
}

static void* jvm_on_error(envoy_error error, envoy_stream_intel stream_intel,
                          envoy_final_stream_intel final_stream_intel, void* context) {
  void* result = call_jvm_on_error(error, stream_intel, final_stream_intel, context);
  Envoy::JNI::jniDeleteGlobalRef(context);
  return result;
}

static void* call_jvm_on_cancel(envoy_stream_intel stream_intel,
                                envoy_final_stream_intel final_stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_cancel");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmObserverContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onCancel =
      jni_helper.getMethodId(jcls_JvmObserverContext.get(), "onCancel", "([J[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_final_stream_intel =
      Envoy::JNI::envoyFinalStreamIntelToJavaLongArray(jni_helper, final_stream_intel);

  jobject result = jni_helper
                       .callObjectMethod(j_context, jmid_onCancel, j_stream_intel.get(),
                                         j_final_stream_intel.get())
                       .release();

  return result;
}

static void* jvm_on_complete(envoy_stream_intel stream_intel,
                             envoy_final_stream_intel final_stream_intel, void* context) {
  void* result = call_jvm_on_complete(stream_intel, final_stream_intel, context);
  Envoy::JNI::jniDeleteGlobalRef(context);
  return result;
}

static void* jvm_on_cancel(envoy_stream_intel stream_intel,
                           envoy_final_stream_intel final_stream_intel, void* context) {
  void* result = call_jvm_on_cancel(stream_intel, final_stream_intel, context);
  Envoy::JNI::jniDeleteGlobalRef(context);
  return result;
}

static void jvm_http_filter_on_error(envoy_error error, envoy_stream_intel stream_intel,
                                     envoy_final_stream_intel final_stream_intel,
                                     const void* context) {
  call_jvm_on_error(error, stream_intel, final_stream_intel, const_cast<void*>(context));
}

static void jvm_http_filter_on_cancel(envoy_stream_intel stream_intel,
                                      envoy_final_stream_intel final_stream_intel,
                                      const void* context) {
  call_jvm_on_cancel(stream_intel, final_stream_intel, const_cast<void*>(context));
}

static void* jvm_on_send_window_available(envoy_stream_intel stream_intel, void* context) {
  jni_log("[Envoy]", "jvm_on_send_window_available");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmObserverContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onSendWindowAvailable = jni_helper.getMethodId(
      jcls_JvmObserverContext.get(), "onSendWindowAvailable", "([J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);

  jobject result =
      jni_helper.callObjectMethod(j_context, jmid_onSendWindowAvailable, j_stream_intel.get())
          .release();

  return result;
}

// JvmKeyValueStoreContext
static envoy_data jvm_kv_store_read(envoy_data key, const void* context) {
  jni_log("[Envoy]", "jvm_kv_store_read");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmKeyValueStoreContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_read =
      jni_helper.getMethodId(jcls_JvmKeyValueStoreContext.get(), "read", "([B)[B");
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_key =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, key);
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_value =
      jni_helper.callObjectMethod<jbyteArray>(j_context, jmid_read, j_key.get());
  envoy_data native_data = Envoy::JNI::javaByteArrayToEnvoyData(jni_helper, j_value.get());

  return native_data;
}

static void jvm_kv_store_remove(envoy_data key, const void* context) {
  jni_log("[Envoy]", "jvm_kv_store_remove");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmKeyValueStoreContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_remove =
      jni_helper.getMethodId(jcls_JvmKeyValueStoreContext.get(), "remove", "([B)V");
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_key =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, key);
  jni_helper.callVoidMethod(j_context, jmid_remove, j_key.get());
}

static void jvm_kv_store_save(envoy_data key, envoy_data value, const void* context) {
  jni_log("[Envoy]", "jvm_kv_store_save");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());

  jobject j_context = static_cast<jobject>(const_cast<void*>(context));

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmKeyValueStoreContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_save =
      jni_helper.getMethodId(jcls_JvmKeyValueStoreContext.get(), "save", "([B[B)V");
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_key =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, key);
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_value =
      Envoy::JNI::envoyDataToJavaByteArray(jni_helper, value);
  jni_helper.callVoidMethod(j_context, jmid_save, j_key.get(), j_value.get());
}

// JvmFilterFactoryContext

static const void* jvm_http_filter_init(const void* context) {
  jni_log("[Envoy]", "jvm_filter_init");

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());

  envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
  jobject j_context = static_cast<jobject>(const_cast<void*>(c_filter->static_context));

  jni_log_fmt("[Envoy]", "j_context: %p", j_context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmFilterFactoryContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_create =
      jni_helper.getMethodId(jcls_JvmFilterFactoryContext.get(), "create",
                             "()Lio/envoyproxy/envoymobile/engine/JvmFilterContext;");

  Envoy::JNI::LocalRefUniquePtr<jobject> j_filter =
      jni_helper.callObjectMethod(j_context, jmid_create);
  jni_log_fmt("[Envoy]", "j_filter: %p", j_filter.get());
  Envoy::JNI::GlobalRefUniquePtr<jobject> retained_filter = jni_helper.newGlobalRef(j_filter.get());

  return retained_filter.release();
}

// EnvoyStringAccessor

static envoy_data jvm_get_string(const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmStringAccessorContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_getString =
      jni_helper.getMethodId(jcls_JvmStringAccessorContext.get(), "getEnvoyString", "()[B");
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_data =
      jni_helper.callObjectMethod<jbyteArray>(j_context, jmid_getString);
  envoy_data native_data = Envoy::JNI::javaByteArrayToEnvoyData(jni_helper, j_data.get());

  return native_data;
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initStream(
    JNIEnv* env, jclass, jlong engine_handle) {

  return init_stream(static_cast<envoy_engine_t>(engine_handle));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_startStream(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject j_context,
    jboolean explicit_flow_control) {

  // TODO: To be truly safe we may need stronger guarantees of operation ordering on this ref.
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_callbacks native_callbacks = {jvm_on_response_headers,
                                           jvm_on_response_data,
                                           jvm_on_metadata,
                                           jvm_on_response_trailers,
                                           jvm_on_error,
                                           jvm_on_complete,
                                           jvm_on_cancel,
                                           jvm_on_send_window_available,
                                           retained_context};
  envoy_status_t result = start_stream(static_cast<envoy_engine_t>(engine_handle),
                                       static_cast<envoy_stream_t>(stream_handle), native_callbacks,
                                       explicit_flow_control);
  if (result != ENVOY_SUCCESS) {
    env->DeleteGlobalRef(retained_context); // No callbacks are fired and we need to release
  }
  return result;
}

// EnvoyKeyValueStore

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerKeyValueStore(JNIEnv* env, jclass,
                                                                       jstring name,
                                                                       jobject j_context) {

  // TODO(goaway): The java context here leaks, but it's tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  jni_log("[Envoy]", "registerKeyValueStore");
  jni_log_fmt("[Envoy]", "j_context: %p", j_context);
  jobject retained_context = env->NewGlobalRef(j_context);
  jni_log_fmt("[Envoy]", "retained_context: %p", retained_context);
  envoy_kv_store* api = (envoy_kv_store*)safe_malloc(sizeof(envoy_kv_store));
  api->save = jvm_kv_store_save;
  api->read = jvm_kv_store_read;
  api->remove = jvm_kv_store_remove;
  api->context = retained_context;

  envoy_status_t result = register_platform_api(env->GetStringUTFChars(name, nullptr), api);
  return result;
}

// EnvoyHTTPFilter

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerFilterFactory(JNIEnv* env, jclass,
                                                                       jstring filter_name,
                                                                       jobject j_context) {

  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  jni_log("[Envoy]", "registerFilterFactory");
  jni_log_fmt("[Envoy]", "j_context: %p", j_context);
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
  api->release_filter = Envoy::JNI::jniDeleteConstGlobalRef;
  api->static_context = retained_context;
  api->instance_context = nullptr;

  envoy_status_t result = register_platform_api(env->GetStringUTFChars(filter_name, nullptr), api);
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
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jlong byte_count) {
  return read_data(static_cast<envoy_engine_t>(engine_handle),
                   static_cast<envoy_stream_t>(stream_handle), byte_count);
}

// The Java counterpart guarantees to invoke this method with a non-null direct ByteBuffer where the
// provided length is between 0 and ByteBuffer.capacity(), inclusively.
extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject data, jint length,
    jboolean end_stream) {
  Envoy::JNI::JniHelper jni_helper(env);
  if (end_stream) {
    jni_log("[Envoy]", "jvm_send_data_end_stream");
  }
  return send_data(static_cast<envoy_engine_t>(engine_handle),
                   static_cast<envoy_stream_t>(stream_handle),
                   Envoy::JNI::javaByteBufferToEnvoyData(jni_helper, data, length), end_stream);
}

// The Java counterpart guarantees to invoke this method with a non-null jbyteArray where the
// provided length is between 0 and the size of the jbyteArray, inclusively. And given that this
// jbyteArray comes from a ByteBuffer, it is also guaranteed that its length will not be greater
// than 2^31 - this is why the length type is jint.
extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendDataByteArray(JNIEnv* env, jclass,
                                                                   jlong engine_handle,
                                                                   jlong stream_handle,
                                                                   jbyteArray data, jint length,
                                                                   jboolean end_stream) {
  Envoy::JNI::JniHelper jni_helper(env);
  if (end_stream) {
    jni_log("[Envoy]", "jvm_send_data_end_stream");
  }
  return send_data(static_cast<envoy_engine_t>(engine_handle),
                   static_cast<envoy_stream_t>(stream_handle),
                   Envoy::JNI::javaByteArrayToEnvoyData(jni_helper, data, length), end_stream);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendHeaders(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobjectArray headers,
    jboolean end_stream) {
  Envoy::JNI::JniHelper jni_helper(env);
  return send_headers(
      static_cast<envoy_engine_t>(engine_handle), static_cast<envoy_stream_t>(stream_handle),
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, headers), end_stream);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendTrailers(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobjectArray trailers) {
  Envoy::JNI::JniHelper jni_helper(env);
  jni_log("[Envoy]", "jvm_send_trailers");
  return send_trailers(static_cast<envoy_engine_t>(engine_handle),
                       static_cast<envoy_stream_t>(stream_handle),
                       Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, trailers));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetStream(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle) {
  return reset_stream(static_cast<envoy_engine_t>(engine_handle),
                      static_cast<envoy_stream_t>(stream_handle));
}

// EnvoyStringAccessor

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerStringAccessor(JNIEnv* env, jclass,
                                                                        jstring accessor_name,
                                                                        jobject j_context) {

  // TODO(goaway): The retained_context leaks, but it's tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332.
  jobject retained_context = env->NewGlobalRef(j_context);

  envoy_string_accessor* string_accessor =
      (envoy_string_accessor*)safe_malloc(sizeof(envoy_string_accessor));
  string_accessor->get_string = jvm_get_string;
  string_accessor->context = retained_context;

  envoy_status_t result =
      register_platform_api(env->GetStringUTFChars(accessor_name, nullptr), string_accessor);
  return result;
}

// Takes a jstring from Java, converts it to a C++ string, calls the supplied
// setter on it.
void setString(Envoy::JNI::JniHelper& jni_helper, jstring java_string, EngineBuilder* builder,
               EngineBuilder& (EngineBuilder::*setter)(std::string)) {
  if (!java_string) {
    return;
  }
  Envoy::JNI::StringUtfUniquePtr native_java_string =
      jni_helper.getStringUtfChars(java_string, nullptr);
  std::string java_string_str(native_java_string.get());
  if (!java_string_str.empty()) {
    (builder->*setter)(java_string_str);
  }
}

// Converts a java byte array to a C++ string.
std::string javaByteArrayToString(Envoy::JNI::JniHelper& jni_helper, jbyteArray j_data) {
  size_t data_length = static_cast<size_t>(jni_helper.getArrayLength(j_data));
  Envoy::JNI::PrimitiveArrayCriticalUniquePtr<char> critical_data =
      jni_helper.getPrimitiveArrayCritical<char*>(j_data, nullptr);
  std::string ret(critical_data.get(), data_length);
  return ret;
}

// Converts a java object array to C++ vector of strings.
std::vector<std::string> javaObjectArrayToStringVector(Envoy::JNI::JniHelper& jni_helper,
                                                       jobjectArray entries) {
  std::vector<std::string> ret;
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_map_entry.
  envoy_map_size_t length = jni_helper.getArrayLength(entries);
  if (length == 0) {
    return ret;
  }

  for (envoy_map_size_t i = 0; i < length; ++i) {
    // Copy native byte array for header key
    Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_str =
        jni_helper.getObjectArrayElement<jbyteArray>(entries, i);
    std::string str = javaByteArrayToString(jni_helper, j_str.get());
    ret.push_back(javaByteArrayToString(jni_helper, j_str.get()));
  }

  return ret;
}

// Converts a java object array to C++ vector of pairs of strings.
std::vector<std::pair<std::string, std::string>>
javaObjectArrayToStringPairVector(Envoy::JNI::JniHelper& jni_helper, jobjectArray entries) {
  std::vector<std::pair<std::string, std::string>> ret;
  // Note that headers is a flattened array of key/value pairs.
  // Therefore, the length of the native header array is n envoy_data or n/2 envoy_map_entry.
  envoy_map_size_t length = jni_helper.getArrayLength(entries);
  if (length == 0) {
    return ret;
  }

  for (envoy_map_size_t i = 0; i < length; i += 2) {
    // Copy native byte array for header key
    Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_key =
        jni_helper.getObjectArrayElement<jbyteArray>(entries, i);
    Envoy::JNI::LocalRefUniquePtr<jbyteArray> j_value =
        jni_helper.getObjectArrayElement<jbyteArray>(entries, i + 1);
    std::string first = javaByteArrayToString(jni_helper, j_key.get());
    std::string second = javaByteArrayToString(jni_helper, j_value.get());
    ret.push_back(std::make_pair(first, second));
  }

  return ret;
}

void configureBuilder(Envoy::JNI::JniHelper& jni_helper, jlong connect_timeout_seconds,
                      jlong dns_refresh_seconds, jlong dns_failure_refresh_seconds_base,
                      jlong dns_failure_refresh_seconds_max, jlong dns_query_timeout_seconds,
                      jlong dns_min_refresh_seconds, jobjectArray dns_preresolve_hostnames,
                      jboolean enable_dns_cache, jlong dns_cache_save_interval_seconds,
                      jboolean enable_drain_post_dns_refresh, jboolean enable_http3,
                      jstring http3_connection_options, jstring http3_client_connection_options,
                      jobjectArray quic_hints, jobjectArray quic_canonical_suffixes,
                      jboolean enable_gzip_decompression, jboolean enable_brotli_decompression,
                      jboolean enable_socket_tagging, jboolean enable_interface_binding,
                      jlong h2_connection_keepalive_idle_interval_milliseconds,
                      jlong h2_connection_keepalive_timeout_seconds, jlong max_connections_per_host,
                      jlong stream_idle_timeout_seconds, jlong per_try_idle_timeout_seconds,
                      jstring app_version, jstring app_id, jboolean trust_chain_verification,
                      jobjectArray filter_chain, jboolean enable_platform_certificates_validation,
                      jobjectArray runtime_guards, jstring node_id, jstring node_region,
                      jstring node_zone, jstring node_sub_zone, jbyteArray serialized_node_metadata,
                      Envoy::Platform::EngineBuilder& builder) {
  builder.addConnectTimeoutSeconds((connect_timeout_seconds));
  builder.addDnsRefreshSeconds((dns_refresh_seconds));
  builder.addDnsFailureRefreshSeconds((dns_failure_refresh_seconds_base),
                                      (dns_failure_refresh_seconds_max));
  builder.addDnsQueryTimeoutSeconds((dns_query_timeout_seconds));
  builder.addDnsMinRefreshSeconds((dns_min_refresh_seconds));
  builder.enableDnsCache(enable_dns_cache == JNI_TRUE, dns_cache_save_interval_seconds);
  builder.addMaxConnectionsPerHost((max_connections_per_host));
  builder.addH2ConnectionKeepaliveIdleIntervalMilliseconds(
      (h2_connection_keepalive_idle_interval_milliseconds));
  builder.addH2ConnectionKeepaliveTimeoutSeconds((h2_connection_keepalive_timeout_seconds));

  setString(jni_helper, app_version, &builder, &EngineBuilder::setAppVersion);
  setString(jni_helper, app_id, &builder, &EngineBuilder::setAppId);
  builder.setDeviceOs("Android");

  builder.setStreamIdleTimeoutSeconds((stream_idle_timeout_seconds));
  builder.setPerTryIdleTimeoutSeconds((per_try_idle_timeout_seconds));
  builder.enableGzipDecompression(enable_gzip_decompression == JNI_TRUE);
  builder.enableBrotliDecompression(enable_brotli_decompression == JNI_TRUE);
  builder.enableSocketTagging(enable_socket_tagging == JNI_TRUE);
#ifdef ENVOY_ENABLE_QUIC
  builder.enableHttp3(enable_http3 == JNI_TRUE);
  builder.setHttp3ConnectionOptions(
      Envoy::JNI::javaStringToString(jni_helper, http3_connection_options));
  builder.setHttp3ClientConnectionOptions(
      Envoy::JNI::javaStringToString(jni_helper, http3_client_connection_options));
  auto hints = javaObjectArrayToStringPairVector(jni_helper, quic_hints);
  for (const std::pair<std::string, std::string>& entry : hints) {
    builder.addQuicHint(entry.first, stoi(entry.second));
  }
  std::vector<std::string> suffixes =
      javaObjectArrayToStringVector(jni_helper, quic_canonical_suffixes);
  for (const std::string& suffix : suffixes) {
    builder.addQuicCanonicalSuffix(suffix);
  }

#endif
  builder.enableInterfaceBinding(enable_interface_binding == JNI_TRUE);
  builder.enableDrainPostDnsRefresh(enable_drain_post_dns_refresh == JNI_TRUE);
  builder.enforceTrustChainVerification(trust_chain_verification == JNI_TRUE);
  builder.enablePlatformCertificatesValidation(enable_platform_certificates_validation == JNI_TRUE);
  builder.setForceAlwaysUsev6(true);

  auto guards = javaObjectArrayToStringPairVector(jni_helper, runtime_guards);
  for (std::pair<std::string, std::string>& entry : guards) {
    builder.setRuntimeGuard(entry.first, entry.second == "true");
  }

  auto filters = javaObjectArrayToStringPairVector(jni_helper, filter_chain);
  for (std::pair<std::string, std::string>& filter : filters) {
    builder.addNativeFilter(filter.first, filter.second);
  }

  std::vector<std::string> hostnames =
      javaObjectArrayToStringVector(jni_helper, dns_preresolve_hostnames);
  builder.addDnsPreresolveHostnames(hostnames);
  std::string native_node_id = Envoy::JNI::javaStringToString(jni_helper, node_id);
  if (!native_node_id.empty()) {
    builder.setNodeId(native_node_id);
  }
  std::string native_node_region = Envoy::JNI::javaStringToString(jni_helper, node_region);
  if (!native_node_region.empty()) {
    builder.setNodeLocality(native_node_region,
                            Envoy::JNI::javaStringToString(jni_helper, node_zone),
                            Envoy::JNI::javaStringToString(jni_helper, node_sub_zone));
  }
  Envoy::ProtobufWkt::Struct node_metadata;
  Envoy::JNI::javaByteArrayToProto(jni_helper, serialized_node_metadata, &node_metadata);
  builder.setNodeMetadata(node_metadata);
}

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_createBootstrap(
    JNIEnv* env, jclass, jlong connect_timeout_seconds, jlong dns_refresh_seconds,
    jlong dns_failure_refresh_seconds_base, jlong dns_failure_refresh_seconds_max,
    jlong dns_query_timeout_seconds, jlong dns_min_refresh_seconds,
    jobjectArray dns_preresolve_hostnames, jboolean enable_dns_cache,
    jlong dns_cache_save_interval_seconds, jboolean enable_drain_post_dns_refresh,
    jboolean enable_http3, jstring http3_connection_options,
    jstring http3_client_connection_options, jobjectArray quic_hints,
    jobjectArray quic_canonical_suffixes, jboolean enable_gzip_decompression,
    jboolean enable_brotli_decompression, jboolean enable_socket_tagging,
    jboolean enable_interface_binding, jlong h2_connection_keepalive_idle_interval_milliseconds,
    jlong h2_connection_keepalive_timeout_seconds, jlong max_connections_per_host,
    jlong stream_idle_timeout_seconds, jlong per_try_idle_timeout_seconds, jstring app_version,
    jstring app_id, jboolean trust_chain_verification, jobjectArray filter_chain,
    jboolean enable_platform_certificates_validation, jobjectArray runtime_guards,
    jstring rtds_resource_name, jlong rtds_timeout_seconds, jstring xds_address, jlong xds_port,
    jobjectArray xds_grpc_initial_metadata, jstring xds_root_certs, jstring node_id,
    jstring node_region, jstring node_zone, jstring node_sub_zone,
    jbyteArray serialized_node_metadata, jstring cds_resources_locator, jlong cds_timeout_seconds,
    jboolean enable_cds) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::Platform::EngineBuilder builder;

  configureBuilder(jni_helper, connect_timeout_seconds, dns_refresh_seconds,
                   dns_failure_refresh_seconds_base, dns_failure_refresh_seconds_max,
                   dns_query_timeout_seconds, dns_min_refresh_seconds, dns_preresolve_hostnames,
                   enable_dns_cache, dns_cache_save_interval_seconds, enable_drain_post_dns_refresh,
                   enable_http3, http3_connection_options, http3_client_connection_options,
                   quic_hints, quic_canonical_suffixes, enable_gzip_decompression,
                   enable_brotli_decompression, enable_socket_tagging, enable_interface_binding,
                   h2_connection_keepalive_idle_interval_milliseconds,
                   h2_connection_keepalive_timeout_seconds, max_connections_per_host,
                   stream_idle_timeout_seconds, per_try_idle_timeout_seconds, app_version, app_id,
                   trust_chain_verification, filter_chain, enable_platform_certificates_validation,
                   runtime_guards, node_id, node_region, node_zone, node_sub_zone,
                   serialized_node_metadata, builder);

  std::string native_xds_address = Envoy::JNI::javaStringToString(jni_helper, xds_address);
  if (!native_xds_address.empty()) {
#ifdef ENVOY_MOBILE_XDS
    Envoy::Platform::XdsBuilder xds_builder(std::move(native_xds_address), xds_port);
    auto initial_metadata =
        javaObjectArrayToStringPairVector(jni_helper, xds_grpc_initial_metadata);
    for (const std::pair<std::string, std::string>& entry : initial_metadata) {
      xds_builder.addInitialStreamHeader(entry.first, entry.second);
    }
    std::string native_root_certs = Envoy::JNI::javaStringToString(jni_helper, xds_root_certs);
    if (!native_root_certs.empty()) {
      xds_builder.setSslRootCerts(std::move(native_root_certs));
    }
    std::string native_rtds_resource_name =
        Envoy::JNI::javaStringToString(jni_helper, rtds_resource_name);
    if (!native_rtds_resource_name.empty()) {
      xds_builder.addRuntimeDiscoveryService(std::move(native_rtds_resource_name),
                                             rtds_timeout_seconds);
    }
    if (enable_cds == JNI_TRUE) {
      xds_builder.addClusterDiscoveryService(
          Envoy::JNI::javaStringToString(jni_helper, cds_resources_locator), cds_timeout_seconds);
    }
    builder.setXds(std::move(xds_builder));
#else
    jni_helper.throwNew("java/lang/UnsupportedOperationException",
                        "This library does not support xDS. Please use "
                        "io.envoyproxy.envoymobile:envoy-xds instead.");
#endif
  }

  return reinterpret_cast<intptr_t>(builder.generateBootstrap().release());
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetConnectivityState(JNIEnv* env,
                                                                        jclass, // class
                                                                        jlong engine) {
  jni_log("[Envoy]", "resetConnectivityState");
  return reset_connectivity_state(engine);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_setPreferredNetwork(JNIEnv* env,
                                                                     jclass, // class
                                                                     jlong engine, jint network) {
  jni_log("[Envoy]", "setting preferred network");
  return set_preferred_network(static_cast<envoy_engine_t>(engine),
                               static_cast<envoy_network_t>(network));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_setProxySettings(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring host, jint port) {
  jni_log("[Envoy]", "setProxySettings");

  const char* native_host = env->GetStringUTFChars(host, nullptr);
  const uint16_t native_port = static_cast<uint16_t>(port);

  envoy_status_t result =
      set_proxy_settings(static_cast<envoy_engine_t>(engine), native_host, native_port);

  env->ReleaseStringUTFChars(host, native_host);
  return result;
}

static void jvm_add_test_root_certificate(const uint8_t* cert, size_t len) {
  jni_log("[Envoy]", "jvm_add_test_root_certificate");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_AndroidNetworkLibrary =
      Envoy::JNI::findClass("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_addTestRootCertificate = jni_helper.getStaticMethodId(
      jcls_AndroidNetworkLibrary.get(), "addTestRootCertificate", "([B)V");

  Envoy::JNI::LocalRefUniquePtr<jbyteArray> cert_array =
      Envoy::JNI::byteArrayToJavaByteArray(jni_helper, cert, len);
  jni_helper.callStaticVoidMethod(jcls_AndroidNetworkLibrary.get(), jmid_addTestRootCertificate,
                                  cert_array.get());
}

static void jvm_clear_test_root_certificate() {
  jni_log("[Envoy]", "jvm_clear_test_root_certificate");
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::getEnv());
  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_AndroidNetworkLibrary =
      Envoy::JNI::findClass("io.envoyproxy.envoymobile.utilities.AndroidNetworkLibrary");
  jmethodID jmid_clearTestRootCertificates = jni_helper.getStaticMethodId(
      jcls_AndroidNetworkLibrary.get(), "clearTestRootCertificates", "()V");

  jni_helper.callStaticVoidMethod(jcls_AndroidNetworkLibrary.get(), jmid_clearTestRootCertificates);
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_callCertificateVerificationFromNative(
    JNIEnv* env, jclass, jobjectArray certChain, jbyteArray jauthType, jbyteArray jhost) {
  Envoy::JNI::JniHelper jni_helper(env);
  std::vector<std::string> cert_chain;
  std::string auth_type;
  std::string host;

  Envoy::JNI::javaArrayOfByteArrayToStringVector(jni_helper, certChain, &cert_chain);
  Envoy::JNI::javaByteArrayToString(jni_helper, jauthType, &auth_type);
  Envoy::JNI::javaByteArrayToString(jni_helper, jhost, &host);

  return callJvmVerifyX509CertChain(jni_helper, cert_chain, auth_type, host).release();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_callAddTestRootCertificateFromNative(
    JNIEnv* env, jclass, jbyteArray jcert) {
  Envoy::JNI::JniHelper jni_helper(env);
  std::vector<uint8_t> cert;
  Envoy::JNI::javaByteArrayToByteVector(jni_helper, jcert, &cert);
  jvm_add_test_root_certificate(cert.data(), cert.size());
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_callClearTestRootCertificateFromNative(JNIEnv*,
                                                                                        jclass) {
  jvm_clear_test_root_certificate();
}
