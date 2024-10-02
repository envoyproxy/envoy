#include <cstddef>
#include <string>

#include "source/common/protobuf/protobuf.h"

#include "ares.h"
#include "library/cc/engine_builder.h"
#include "library/common/api/c_types.h"
#include "library/common/bridge/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/c_types.h"
#include "library/common/extensions/key_value/platform/c_types.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"
#include "library/common/types/managed_envoy_headers.h"
#include "library/jni/android_network_utility.h"
#include "library/jni/jni_helper.h"
#include "library/jni/jni_utility.h"

using Envoy::Platform::EngineBuilder;

// NOLINT(namespace-envoy)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
  Envoy::JNI::JniHelper::initialize(vm);
  Envoy::JNI::JniUtility::initCache();
  Envoy::JNI::JniHelper::addToCache(
      "io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary",
      /* methods= */ {},
      /* static_methods= */
      {
          {"isCleartextTrafficPermitted", "(Ljava/lang/String;)Z"},
          {"tagSocket", "(III)V"},
          {"verifyServerCertificates",
           "([[B[B[B)Lio/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult;"},
          {"addTestRootCertificate", "([B)V"},
          {"clearTestRootCertificates", "()V"},

      },
      /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/utilities/AndroidCertVerifyResult",
                                    /* methods= */
                                    {
                                        {"isIssuedByKnownRoot", "()Z"},
                                        {"getStatus", "()I"},
                                        {"getCertificateChainEncoded", "()[[B"},
                                    },
                                    /* static_methods= */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyOnEngineRunning",
                                    /* methods= */
                                    {
                                        {"invokeOnEngineRunning", "()Ljava/lang/Object;"},
                                    },
                                    /* static_methods= */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyLogger",
                                    /* methods= */
                                    {
                                        {"log", "(ILjava/lang/String;)V"},
                                    },
                                    /* static_methods= */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache("io/envoyproxy/envoymobile/engine/types/EnvoyEventTracker",
                                    /* methods= */
                                    {
                                        {"track", "(Ljava/util/Map;)V"},
                                    },
                                    /* static_methods= */ {},
                                    /* fields= */ {}, /* static_fields= */ {});
  Envoy::JNI::JniHelper::addToCache(
      "io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks",
      /* methods= */
      {
          {"onHeaders",
           "(Ljava/util/Map;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onData",
           "(Ljava/nio/ByteBuffer;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onTrailers",
           "(Ljava/util/Map;Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
          {"onComplete", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/envoyproxy/"
                         "envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onError",
           "(ILjava/lang/String;ILio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/"
           "envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onCancel", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;Lio/envoyproxy/"
                       "envoymobile/engine/types/EnvoyFinalStreamIntel;)V"},
          {"onSendWindowAvailable", "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V"},
      },
      /* static_methods= */ {},
      /* fields= */ {}, /* static_fields= */ {});
  return Envoy::JNI::JniHelper::getVersion();
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void* /* reserved */) {
  Envoy::JNI::JniHelper::finalize();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_setLogLevel(JNIEnv* /*env*/, jclass, jint level) {
  Envoy::Logger::Context::changeAllLogLevels(static_cast<spdlog::level::level_enum>(level));
}

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initEngine(
    JNIEnv* env, jclass, jobject on_engine_running, jobject envoy_logger,
    jobject envoy_event_tracker) {
  //================================================================================================
  // EngineCallbacks
  //================================================================================================
  std::unique_ptr<Envoy::EngineCallbacks> callbacks = std::make_unique<Envoy::EngineCallbacks>();
  if (on_engine_running != nullptr) {
    jobject on_engine_running_global_ref = env->NewGlobalRef(on_engine_running);
    callbacks->on_engine_running_ = [on_engine_running_global_ref] {
      Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
      auto java_on_engine_running_class = jni_helper.findClassFromCache(
          "io/envoyproxy/envoymobile/engine/types/EnvoyOnEngineRunning");
      jmethodID java_on_engine_running_method_id = jni_helper.getMethodIdFromCache(
          java_on_engine_running_class, "invokeOnEngineRunning", "()Ljava/lang/Object;");
      Envoy::JNI::LocalRefUniquePtr<jobject> unused = jni_helper.callObjectMethod(
          on_engine_running_global_ref, java_on_engine_running_method_id);
      jni_helper.getEnv()->DeleteGlobalRef(on_engine_running_global_ref);
    };
    callbacks->on_exit_ = [] {
      // Note that this is not dispatched because the thread that
      // needs to be detached is the engine thread.
      // This function is called from the context of the engine's
      // thread due to it being posted to the engine's event dispatcher.
      Envoy::JNI::JniHelper::detachCurrentThread();
    };
  }
  //================================================================================================
  // EnvoyLogger
  //================================================================================================
  std::unique_ptr<Envoy::EnvoyLogger> logger = std::make_unique<Envoy::EnvoyLogger>();
  if (envoy_logger != nullptr) {
    jobject envoy_logger_global_ref = env->NewGlobalRef(envoy_logger);
    logger->on_log_ = [envoy_logger_global_ref](Envoy::Logger::Logger::Levels level,
                                                const std::string& message) {
      Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
      Envoy::JNI::LocalRefUniquePtr<jstring> java_message =
          Envoy::JNI::cppStringToJavaString(jni_helper, message);
      jint java_level = static_cast<jint>(level);
      auto java_envoy_logger_class =
          jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyLogger");
      jmethodID java_log_method_id =
          jni_helper.getMethodIdFromCache(java_envoy_logger_class, "log", "(ILjava/lang/String;)V");
      jni_helper.callVoidMethod(envoy_logger_global_ref, java_log_method_id, java_level,
                                java_message.get());
    };
    logger->on_exit_ = [envoy_logger_global_ref] {
      Envoy::JNI::JniHelper::getThreadLocalEnv()->DeleteGlobalRef(envoy_logger_global_ref);
    };
  }
  //================================================================================================
  // EnvoyEventTracker
  //================================================================================================
  std::unique_ptr<Envoy::EnvoyEventTracker> event_tracker =
      std::make_unique<Envoy::EnvoyEventTracker>();
  if (envoy_event_tracker != nullptr) {
    jobject event_tracker_global_ref = env->NewGlobalRef(envoy_event_tracker);
    event_tracker->on_track_ = [event_tracker_global_ref](
                                   const absl::flat_hash_map<std::string, std::string>& events) {
      Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
      Envoy::JNI::LocalRefUniquePtr<jobject> java_events =
          Envoy::JNI::cppMapToJavaMap(jni_helper, events);
      auto java_envoy_event_tracker_class =
          jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyEventTracker");
      jmethodID java_track_method_id = jni_helper.getMethodIdFromCache(
          java_envoy_event_tracker_class, "track", "(Ljava/util/Map;)V");
      jni_helper.callVoidMethod(event_tracker_global_ref, java_track_method_id, java_events.get());
    };
    event_tracker->on_exit_ = [event_tracker_global_ref] {
      Envoy::JNI::JniHelper::getThreadLocalEnv()->DeleteGlobalRef(event_tracker_global_ref);
    };
  }

  return reinterpret_cast<jlong>(
      new Envoy::InternalEngine(std::move(callbacks), std::move(logger), std::move(event_tracker)));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_runEngine(
    JNIEnv* env, jclass, jlong engine, jlong bootstrap_ptr, jstring log_level) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr java_log_level = jni_helper.getStringUtfChars(log_level, nullptr);
  // This should be either 0 (null) or a pointer generated by createBootstrap.
  // As documented in JniLibrary.java, take ownership.
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap(
      reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));

  auto options = std::make_unique<Envoy::OptionsImplBase>();
  options->setConfigProto(std::move(bootstrap));
  ENVOY_BUG(options->setLogLevel(java_log_level.get()).ok(), "invalid log level");
  options->setConcurrency(1);
  jint result = reinterpret_cast<Envoy::InternalEngine*>(engine)->run(std::move(options));

  return result;
}

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_terminateEngine(
    JNIEnv* /*env*/, jclass, jlong engine_handle) {
  Envoy::InternalEngine* internal_engine = reinterpret_cast<Envoy::InternalEngine*>(engine_handle);
  internal_engine->terminate();
  delete internal_engine;
}

extern "C" JNIEXPORT void JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_initCares(
    JNIEnv* env, jclass, jobject connectivity_manager) {
#if defined(__ANDROID_API__)
  Envoy::JNI::JniHelper jni_helper(env);
  ares_library_init_jvm(jni_helper.getJavaVm());
  ares_library_init_android(connectivity_manager);
#else
  // For suppressing unused parameters.
  (void)env;
  (void)connectivity_manager;
#endif
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_runtimeFeatureEnabled(JNIEnv* env, jclass,
                                                                       jstring feature_name) {
  Envoy::JNI::JniHelper jni_helper(env);
  return Envoy::Runtime::runtimeFeatureEnabled(
      Envoy::JNI::javaStringToCppString(jni_helper, feature_name));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_recordCounterInc(
    JNIEnv* env,
    jclass, // class
    jlong engine_handle, jstring elements, jobjectArray tags, jint count) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr native_elements = jni_helper.getStringUtfChars(elements, nullptr);
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->recordCounterInc(native_elements.get(),
                         Envoy::JNI::javaArrayOfObjectArrayToEnvoyStatsTags(jni_helper, tags),
                         count);
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_dumpStats(JNIEnv* env,
                                                           jclass, // class
                                                           jlong engine_handle) {
  auto engine = reinterpret_cast<Envoy::InternalEngine*>(engine_handle);
  std::string stats = engine->dumpStats();
  Envoy::JNI::JniHelper jni_helper(env);
  return jni_helper.newStringUtf(stats.c_str()).release();
}

// JvmCallbackContext

static void passHeaders(const char* method, const Envoy::Types::ManagedEnvoyHeaders& headers,
                        jobject j_context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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

static Envoy::JNI::LocalRefUniquePtr<jobjectArray>
jvm_on_headers(const char* method, const Envoy::Types::ManagedEnvoyHeaders& headers,
               bool end_stream, envoy_stream_intel stream_intel, void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jni_helper.callObjectMethod<jobjectArray>(
      j_context, jmid_onHeaders, static_cast<jlong>(headers.get().length),
      end_stream ? JNI_TRUE : JNI_FALSE, j_stream_intel.get());

  if (!jni_helper.exceptionCheck()) {
    return result;
  }
  // TODO(fredyw): The whole code here is a bit weird, but this how it currently works. Consider
  // rewriting it.
  auto java_exception = jni_helper.exceptionOccurred();
  jni_helper.exceptionCleared();
  std::string java_exception_message =
      Envoy::JNI::getJavaExceptionMessage(jni_helper, java_exception.get());
  ENVOY_LOG_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "jni_cleared_pending_exception", "{}",
                            absl::StrFormat("%s||%s||", java_exception_message, method));

  // Create a "no operation" result:
  //  1. Tell the filter chain to continue the iteration.
  //  2. Return headers received on as method's input as part of the method's output.
  jclass jcls_object_array = jni_helper.findClassFromCache("java/lang/Object");
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> noopResult =
      jni_helper.newObjectArray(2, jcls_object_array, NULL);

  jclass jcls_int = jni_helper.findClassFromCache("java/lang/Integer");
  jmethodID jmid_intInit = jni_helper.getMethodIdFromCache(jcls_int, "<init>", "(I)V");
  Envoy::JNI::LocalRefUniquePtr<jobject> j_status = jni_helper.newObject(jcls_int, jmid_intInit, 0);
  // Set status to "0" (FilterHeadersStatus::Continue). Signal that the intent
  // is to continue the iteration of the filter chain.
  jni_helper.setObjectArrayElement(noopResult.get(), 0, j_status.get());

  // Since the "on headers" call threw an exception set input headers as output headers.
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      Envoy::JNI::envoyHeadersToJavaArrayOfObjectArray(jni_helper, headers);
  jni_helper.setObjectArrayElement(noopResult.get(), 1, j_headers.get());

  return noopResult;
}

static envoy_filter_headers_status
jvm_http_filter_on_request_headers(envoy_headers input_headers, bool end_stream,
                                   envoy_stream_intel stream_intel, const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  const auto headers = Envoy::Types::ManagedEnvoyHeaders(input_headers);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jvm_on_headers(
      "onRequestHeaders", headers, end_stream, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                         /*headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_headers native_headers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_headers.get());

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
}

static envoy_filter_headers_status
jvm_http_filter_on_response_headers(envoy_headers input_headers, bool end_stream,
                                    envoy_stream_intel stream_intel, const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  const auto headers = Envoy::Types::ManagedEnvoyHeaders(input_headers);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jvm_on_headers(
      "onResponseHeaders", headers, end_stream, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                         /*headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_headers native_headers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_headers.get());

  return (envoy_filter_headers_status){/*status*/ unboxed_status,
                                       /*headers*/ native_headers};
}

static Envoy::JNI::LocalRefUniquePtr<jobjectArray> jvm_on_data(const char* method, envoy_data data,
                                                               bool end_stream,
                                                               envoy_stream_intel stream_intel,
                                                               void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onData = jni_helper.getMethodId(jcls_JvmCallbackContext.get(), method,
                                                 "(Ljava/nio/ByteBuffer;Z[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jobject> j_data =
      Envoy::JNI::envoyDataToJavaByteBuffer(jni_helper, data);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jni_helper.callObjectMethod<jobjectArray>(
      j_context, jmid_onData, j_data.get(), end_stream ? JNI_TRUE : JNI_FALSE,
      j_stream_intel.get());

  release_envoy_data(data);

  return result;
}

static envoy_filter_data_status jvm_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                envoy_stream_intel stream_intel,
                                                                const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result =
      jvm_on_data("onRequestData", data, end_stream, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                      /*data*/ {},
                                      /*pending_headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_data =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_data native_data = Envoy::JNI::javaByteBufferToEnvoyData(jni_helper, j_data.get());

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());
  }

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static envoy_filter_data_status jvm_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 envoy_stream_intel stream_intel,
                                                                 const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result =
      jvm_on_data("onResponseData", data, end_stream, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                      /*data*/ {},
                                      /*pending_headers*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_data =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_data native_data = Envoy::JNI::javaByteBufferToEnvoyData(jni_helper, j_data.get());

  envoy_headers* pending_headers = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterDataStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());
  }

  return (envoy_filter_data_status){/*status*/ unboxed_status,
                                    /*data*/ native_data,
                                    /*pending_headers*/ pending_headers};
}

static Envoy::JNI::LocalRefUniquePtr<jobjectArray> jvm_on_trailers(const char* method,
                                                                   envoy_headers trailers,
                                                                   envoy_stream_intel stream_intel,
                                                                   void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result = jni_helper.callObjectMethod<jobjectArray>(
      j_context, jmid_onTrailers, static_cast<jlong>(trailers.length), j_stream_intel.get());

  return result;
}

static envoy_filter_trailers_status
jvm_http_filter_on_request_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                    const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result =
      jvm_on_trailers("onRequestTrailers", trailers, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                          /*trailers*/ {},
                                          /*pending_headers*/ {},
                                          /*pending_data*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_trailers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_headers native_trailers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_trailers.get());

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());

    Envoy::JNI::LocalRefUniquePtr<jobject> j_data =
        jni_helper.getObjectArrayElement(result.get(), 3);
    pending_data = Envoy::JNI::javaByteBufferToEnvoyDataPtr(jni_helper, j_data.get());
  }

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static envoy_filter_trailers_status
jvm_http_filter_on_response_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                     const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> result =
      jvm_on_trailers("onResponseTrailers", trailers, stream_intel, const_cast<void*>(context));

  if (result == nullptr || jni_helper.getArrayLength(result.get()) < 2) {
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterHeadersStatusStopIteration,
                                          /*trailers*/ {},
                                          /*pending_headers*/ {},
                                          /*pending_data*/ {}};
  }

  Envoy::JNI::LocalRefUniquePtr<jobject> status = jni_helper.getObjectArrayElement(result.get(), 0);
  Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_trailers =
      jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 1);

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
  envoy_headers native_trailers =
      Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeaders(jni_helper, j_trailers.get());

  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  // Avoid out-of-bounds access to array when checking for optional pending entities.
  if (unboxed_status == kEnvoyFilterTrailersStatusResumeIteration) {
    Envoy::JNI::LocalRefUniquePtr<jobjectArray> j_headers =
        jni_helper.getObjectArrayElement<jobjectArray>(result.get(), 2);
    pending_headers =
        Envoy::JNI::javaArrayOfObjectArrayToEnvoyHeadersPtr(jni_helper, j_headers.get());

    Envoy::JNI::LocalRefUniquePtr<jobject> j_data =
        jni_helper.getObjectArrayElement(result.get(), 3);
    pending_data = Envoy::JNI::javaByteBufferToEnvoyDataPtr(jni_helper, j_data.get());
  }

  return (envoy_filter_trailers_status){/*status*/ unboxed_status,
                                        /*trailers*/ native_trailers,
                                        /*pending_headers*/ pending_headers,
                                        /*pending_data*/ pending_data};
}

static void jvm_http_filter_set_request_callbacks(envoy_http_filter_callbacks callbacks,
                                                  const void* context) {

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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

  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  jobject j_context = static_cast<jobject>(const_cast<void*>(context));
  jlong headers_length = -1;
  if (headers) {
    headers_length = static_cast<jlong>(headers->length);
    passHeaders("passHeader", *headers, j_context);
  }
  Envoy::JNI::LocalRefUniquePtr<jobject> j_in_data =
      Envoy::JNI::LocalRefUniquePtr<jobject>(nullptr, Envoy::JNI::LocalRefDeleter());
  if (data) {
    j_in_data = Envoy::JNI::envoyDataToJavaByteBuffer(jni_helper, *data);
  }
  jlong trailers_length = -1;
  if (trailers) {
    trailers_length = static_cast<jlong>(trailers->length);
    passHeaders("passTrailer", *trailers, j_context);
  }
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmCallbackContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onResume = jni_helper.getMethodId(
      jcls_JvmCallbackContext.get(), method, "(JLjava/nio/ByteBuffer;JZ[J)Ljava/lang/Object;");
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

  int unboxed_status = Envoy::JNI::javaIntegerToCppInt(jni_helper, status.get());
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

static void call_jvm_on_error(envoy_error error, envoy_stream_intel stream_intel,
                              envoy_final_stream_intel final_stream_intel, void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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

  Envoy::JNI::LocalRefUniquePtr<jobject> unused = jni_helper.callObjectMethod(
      j_context, jmid_onError, error.error_code, j_error_message.get(), error.attempt_count,
      j_stream_intel.get(), j_final_stream_intel.get());

  release_envoy_error(error);
}

static void call_jvm_on_cancel(envoy_stream_intel stream_intel,
                               envoy_final_stream_intel final_stream_intel, void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  jobject j_context = static_cast<jobject>(context);

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmObserverContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_onCancel =
      jni_helper.getMethodId(jcls_JvmObserverContext.get(), "onCancel", "([J[J)Ljava/lang/Object;");

  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_stream_intel =
      Envoy::JNI::envoyStreamIntelToJavaLongArray(jni_helper, stream_intel);
  Envoy::JNI::LocalRefUniquePtr<jlongArray> j_final_stream_intel =
      Envoy::JNI::envoyFinalStreamIntelToJavaLongArray(jni_helper, final_stream_intel);

  Envoy::JNI::LocalRefUniquePtr<jobject> unused = jni_helper.callObjectMethod(
      j_context, jmid_onCancel, j_stream_intel.get(), j_final_stream_intel.get());
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

// JvmKeyValueStoreContext
static envoy_data jvm_kv_store_read(envoy_data key, const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());

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
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());

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
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());

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
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());

  envoy_http_filter* c_filter = static_cast<envoy_http_filter*>(const_cast<void*>(context));
  jobject j_context = static_cast<jobject>(const_cast<void*>(c_filter->static_context));

  Envoy::JNI::LocalRefUniquePtr<jclass> jcls_JvmFilterFactoryContext =
      jni_helper.getObjectClass(j_context);
  jmethodID jmid_create =
      jni_helper.getMethodId(jcls_JvmFilterFactoryContext.get(), "create",
                             "()Lio/envoyproxy/envoymobile/engine/JvmFilterContext;");

  Envoy::JNI::LocalRefUniquePtr<jobject> j_filter =
      jni_helper.callObjectMethod(j_context, jmid_create);
  Envoy::JNI::GlobalRefUniquePtr<jobject> retained_filter = jni_helper.newGlobalRef(j_filter.get());

  return retained_filter.release();
}

// EnvoyStringAccessor

static envoy_data jvm_get_string(const void* context) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
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
    JNIEnv* /*env*/, jclass, jlong engine_handle) {

  auto engine = reinterpret_cast<Envoy::InternalEngine*>(engine_handle);
  return engine->initStream();
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_startStream(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject java_stream_callbacks,
    jboolean explicit_flow_control) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto java_stream_callbacks_global_ref = jni_helper.newGlobalRef(java_stream_callbacks).release();
  auto engine = reinterpret_cast<Envoy::InternalEngine*>(engine_handle);
  Envoy::EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [java_stream_callbacks_global_ref](
                                     const Envoy::Http::ResponseHeaderMap& headers, bool end_stream,
                                     envoy_stream_intel stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_headers = Envoy::JNI::cppHeadersToJavaHeaders(jni_helper, headers);
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_headers_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onHeaders",
        "(Ljava/util/Map;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_headers_method_id,
                              java_headers.get(), static_cast<jboolean>(end_stream),
                              java_stream_intel.get());
  };
  stream_callbacks.on_data_ = [java_stream_callbacks_global_ref](
                                  const Envoy::Buffer::Instance& buffer, uint64_t length,
                                  bool end_stream, envoy_stream_intel stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_byte_buffer =
        Envoy::JNI::cppBufferInstanceToJavaDirectByteBuffer(jni_helper, buffer, length);
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_on_data_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onData",
        "(Ljava/nio/ByteBuffer;ZLio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_data_method_id,
                              java_byte_buffer.get(), static_cast<jboolean>(end_stream),
                              java_stream_intel.get());
  };
  stream_callbacks.on_trailers_ = [java_stream_callbacks_global_ref](
                                      const Envoy::Http::ResponseTrailerMap& trailers,
                                      envoy_stream_intel stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_trailers = Envoy::JNI::cppHeadersToJavaHeaders(jni_helper, trailers);
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_trailers_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onTrailers",
        "(Ljava/util/Map;Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_trailers_method_id,
                              java_trailers.get(), java_stream_intel.get());
  };
  stream_callbacks.on_complete_ = [java_stream_callbacks_global_ref](
                                      envoy_stream_intel stream_intel,
                                      envoy_final_stream_intel final_stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_final_stream_intel =
        Envoy::JNI::cppFinalStreamIntelToJavaFinalStreamIntel(jni_helper, final_stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_complete_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onComplete",
        "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;"
        "Lio/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_complete_method_id,
                              java_stream_intel.get(), java_final_stream_intel.get());
    // on_complete_ is a terminal callback, delete the java_stream_callbacks_global_ref.
    jni_helper.getEnv()->DeleteGlobalRef(java_stream_callbacks_global_ref);
  };
  stream_callbacks.on_error_ = [java_stream_callbacks_global_ref](
                                   const Envoy::EnvoyError& error, envoy_stream_intel stream_intel,
                                   envoy_final_stream_intel final_stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_final_stream_intel =
        Envoy::JNI::cppFinalStreamIntelToJavaFinalStreamIntel(jni_helper, final_stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_error_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onError",
        "(ILjava/lang/String;ILio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;"
        "Lio/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel;)V");
    auto java_error_message = Envoy::JNI::cppStringToJavaString(jni_helper, error.message_);
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_error_method_id,
                              static_cast<jint>(error.error_code_), java_error_message.get(),
                              error.attempt_count_.value_or(-1), java_stream_intel.get(),
                              java_final_stream_intel.get());
    // on_error_ is a terminal callback, delete the java_stream_callbacks_global_ref.
    jni_helper.getEnv()->DeleteGlobalRef(java_stream_callbacks_global_ref);
  };
  stream_callbacks.on_cancel_ = [java_stream_callbacks_global_ref](
                                    envoy_stream_intel stream_intel,
                                    envoy_final_stream_intel final_stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_final_stream_intel =
        Envoy::JNI::cppFinalStreamIntelToJavaFinalStreamIntel(jni_helper, final_stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_cancel_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onCancel",
        "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;"
        "Lio/envoyproxy/envoymobile/engine/types/EnvoyFinalStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref, java_on_cancel_method_id,
                              java_stream_intel.get(), java_final_stream_intel.get());
    // on_cancel_ is a terminal callback, delete the java_stream_callbacks_global_ref.
    jni_helper.getEnv()->DeleteGlobalRef(java_stream_callbacks_global_ref);
  };
  stream_callbacks.on_send_window_available_ = [java_stream_callbacks_global_ref](
                                                   envoy_stream_intel stream_intel) {
    Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
    auto java_stream_intel = Envoy::JNI::cppStreamIntelToJavaStreamIntel(jni_helper, stream_intel);
    auto java_stream_callbacks_class =
        jni_helper.findClassFromCache("io/envoyproxy/envoymobile/engine/types/EnvoyHTTPCallbacks");
    auto java_on_send_window_available_method_id = jni_helper.getMethodIdFromCache(
        java_stream_callbacks_class, "onSendWindowAvailable",
        "(Lio/envoyproxy/envoymobile/engine/types/EnvoyStreamIntel;)V");
    jni_helper.callVoidMethod(java_stream_callbacks_global_ref,
                              java_on_send_window_available_method_id, java_stream_intel.get());
  };

  envoy_status_t result = engine->startStream(static_cast<envoy_stream_t>(stream_handle),
                                              std::move(stream_callbacks), explicit_flow_control);
  if (result != ENVOY_SUCCESS) {
    env->DeleteGlobalRef(
        java_stream_callbacks_global_ref); // No callbacks are fired and we need to release
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
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_kv_store* api = static_cast<envoy_kv_store*>(safe_malloc(sizeof(envoy_kv_store)));
  api->save = jvm_kv_store_save;
  api->read = jvm_kv_store_read;
  api->remove = jvm_kv_store_remove;
  api->context = retained_context;

  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr native_java_string = jni_helper.getStringUtfChars(name, nullptr);
  Envoy::Api::External::registerApi(/*name=*/std::string(native_java_string.get()), api);
  return ENVOY_SUCCESS;
}

// EnvoyHTTPFilter

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_registerFilterFactory(JNIEnv* env, jclass,
                                                                       jstring filter_name,
                                                                       jobject j_context) {

  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  jobject retained_context = env->NewGlobalRef(j_context);
  envoy_http_filter* api = static_cast<envoy_http_filter*>(safe_malloc(sizeof(envoy_http_filter)));
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

  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr native_java_string =
      jni_helper.getStringUtfChars(filter_name, nullptr);
  Envoy::Api::External::registerApi(/*name=*/std::string(native_java_string.get()), api);
  return ENVOY_SUCCESS;
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_EnvoyHTTPFilterCallbacksImpl_callResumeIteration(
    JNIEnv* env, jclass, jlong callback_handle, jobject j_context) {
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
    JNIEnv* /*env*/, jclass, jlong callback_handle) {
  envoy_http_filter_callbacks* callbacks =
      reinterpret_cast<envoy_http_filter_callbacks*>(callback_handle);
  callbacks->release_callbacks(callbacks->callback_context);
  free(callbacks);
}

// EnvoyHTTPStream

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_readData(
    JNIEnv* /*env*/, jclass, jlong engine_handle, jlong stream_handle, jlong byte_count) {
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->readData(static_cast<envoy_stream_t>(stream_handle), byte_count);
}

// This function can accept either direct (off the JVM heap) ByteBuffer or non-direct (on the JVM
// heap) ByteBuffer.
extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendData(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject byte_buffer, jint length,
    jboolean end_stream) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::Buffer::InstancePtr cpp_buffer_instance;
  if (Envoy::JNI::isJavaDirectByteBuffer(jni_helper, byte_buffer)) {
    cpp_buffer_instance =
        Envoy::JNI::javaDirectByteBufferToCppBufferInstance(jni_helper, byte_buffer, length);
  } else {
    cpp_buffer_instance =
        Envoy::JNI::javaNonDirectByteBufferToCppBufferInstance(jni_helper, byte_buffer, length);
  }
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->sendData(static_cast<envoy_stream_t>(stream_handle), std::move(cpp_buffer_instance),
                 end_stream);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendHeaders(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject headers,
    jboolean end_stream, jboolean idempotent) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_headers = Envoy::Http::Utility::createRequestHeaderMapPtr();
  Envoy::JNI::javaHeadersToCppHeaders(jni_helper, headers, *cpp_headers);
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->sendHeaders(static_cast<envoy_stream_t>(stream_handle), std::move(cpp_headers), end_stream,
                    idempotent);
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_sendTrailers(
    JNIEnv* env, jclass, jlong engine_handle, jlong stream_handle, jobject trailers) {
  Envoy::JNI::JniHelper jni_helper(env);
  auto cpp_trailers = Envoy::Http::Utility::createRequestTrailerMapPtr();
  Envoy::JNI::javaHeadersToCppHeaders(jni_helper, trailers, *cpp_trailers);
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->sendTrailers(static_cast<envoy_stream_t>(stream_handle), std::move(cpp_trailers));
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetStream(
    JNIEnv* /*env*/, jclass, jlong engine_handle, jlong stream_handle) {
  return reinterpret_cast<Envoy::InternalEngine*>(engine_handle)
      ->cancelStream(static_cast<envoy_stream_t>(stream_handle));
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
      static_cast<envoy_string_accessor*>(safe_malloc(sizeof(envoy_string_accessor)));
  string_accessor->get_string = jvm_get_string;
  string_accessor->context = retained_context;

  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr native_java_string =
      jni_helper.getStringUtfChars(accessor_name, nullptr);
  Envoy::Api::External::registerApi(/*name=*/std::string(native_java_string.get()),
                                    string_accessor);
  return ENVOY_SUCCESS;
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
                      jint dns_num_retries, jboolean enable_drain_post_dns_refresh,
                      jboolean enable_http3, jboolean use_cares, jboolean force_v6,
                      jboolean use_gro, jstring http3_connection_options,
                      jstring http3_client_connection_options, jobjectArray quic_hints,
                      jobjectArray quic_canonical_suffixes, jboolean enable_gzip_decompression,
                      jboolean enable_brotli_decompression,
                      jlong num_timeouts_to_trigger_port_migration, jboolean enable_socket_tagging,
                      jboolean enable_interface_binding,
                      jlong h2_connection_keepalive_idle_interval_milliseconds,
                      jlong h2_connection_keepalive_timeout_seconds, jlong max_connections_per_host,
                      jlong stream_idle_timeout_seconds, jlong per_try_idle_timeout_seconds,
                      jstring app_version, jstring app_id, jboolean trust_chain_verification,
                      jobjectArray filter_chain, jboolean enable_platform_certificates_validation,
                      jstring upstream_tls_sni, jobjectArray runtime_guards,
                      jobjectArray cares_fallback_resolvers,
                      Envoy::Platform::EngineBuilder& builder) {
  builder.addConnectTimeoutSeconds((connect_timeout_seconds));
  builder.addDnsRefreshSeconds((dns_refresh_seconds));
  builder.addDnsFailureRefreshSeconds((dns_failure_refresh_seconds_base),
                                      (dns_failure_refresh_seconds_max));
  builder.addDnsQueryTimeoutSeconds((dns_query_timeout_seconds));
  builder.addDnsMinRefreshSeconds((dns_min_refresh_seconds));
  builder.enableDnsCache(enable_dns_cache == JNI_TRUE, dns_cache_save_interval_seconds);
  if (dns_num_retries >= 0) {
    builder.setDnsNumRetries(dns_num_retries);
  }
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
  builder.enableHttp3(enable_http3 == JNI_TRUE);
  builder.setHttp3ConnectionOptions(
      Envoy::JNI::javaStringToCppString(jni_helper, http3_connection_options));
  builder.setHttp3ClientConnectionOptions(
      Envoy::JNI::javaStringToCppString(jni_helper, http3_client_connection_options));
  auto hints = javaObjectArrayToStringPairVector(jni_helper, quic_hints);
  for (const std::pair<std::string, std::string>& entry : hints) {
    builder.addQuicHint(entry.first, stoi(entry.second));
  }
  std::vector<std::string> suffixes =
      javaObjectArrayToStringVector(jni_helper, quic_canonical_suffixes);
  for (const std::string& suffix : suffixes) {
    builder.addQuicCanonicalSuffix(suffix);
  }
  builder.setNumTimeoutsToTriggerPortMigration(num_timeouts_to_trigger_port_migration);
  builder.setUseCares(use_cares == JNI_TRUE);
  if (use_cares == JNI_TRUE) {
    auto resolvers = javaObjectArrayToStringPairVector(jni_helper, cares_fallback_resolvers);
    for (const auto& [host, port] : resolvers) {
      builder.addCaresFallbackResolver(host, stoi(port));
    }
  }
  builder.setUseGroIfAvailable(use_gro == JNI_TRUE);
  builder.enableInterfaceBinding(enable_interface_binding == JNI_TRUE);
  builder.enableDrainPostDnsRefresh(enable_drain_post_dns_refresh == JNI_TRUE);
  builder.enforceTrustChainVerification(trust_chain_verification == JNI_TRUE);
  builder.enablePlatformCertificatesValidation(enable_platform_certificates_validation == JNI_TRUE);
  builder.setUpstreamTlsSni(Envoy::JNI::javaStringToCppString(jni_helper, upstream_tls_sni));
  builder.setForceAlwaysUsev6(force_v6 == JNI_TRUE);

  auto guards = javaObjectArrayToStringPairVector(jni_helper, runtime_guards);
  for (std::pair<std::string, std::string>& entry : guards) {
    builder.addRuntimeGuard(entry.first, entry.second == "true");
  }

  auto filters = javaObjectArrayToStringPairVector(jni_helper, filter_chain);
  for (std::pair<std::string, std::string>& filter : filters) {
    builder.addNativeFilter(filter.first, filter.second);
  }

  std::vector<std::string> hostnames =
      javaObjectArrayToStringVector(jni_helper, dns_preresolve_hostnames);
  builder.addDnsPreresolveHostnames(hostnames);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

extern "C" JNIEXPORT jstring JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_getNativeFilterConfig(JNIEnv* env, jclass,
                                                                       jstring filter_name_jstr) {
  Envoy::JNI::JniHelper jni_helper(env);
  std::string filter_name = Envoy::JNI::javaStringToCppString(jni_helper, filter_name_jstr);
  std::string filter_config = EngineBuilder::nativeNameToConfig(filter_name);

  return jni_helper.newStringUtf(filter_config.c_str()).release();
}

extern "C" JNIEXPORT jlong JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_createBootstrap(
    JNIEnv* env, jclass, jlong connect_timeout_seconds, jlong dns_refresh_seconds,
    jlong dns_failure_refresh_seconds_base, jlong dns_failure_refresh_seconds_max,
    jlong dns_query_timeout_seconds, jlong dns_min_refresh_seconds,
    jobjectArray dns_preresolve_hostnames, jboolean enable_dns_cache,
    jlong dns_cache_save_interval_seconds, jint dns_num_retries,
    jboolean enable_drain_post_dns_refresh, jboolean enable_http3, jboolean use_cares,
    jboolean force_v6, jboolean use_gro, jstring http3_connection_options,
    jstring http3_client_connection_options, jobjectArray quic_hints,
    jobjectArray quic_canonical_suffixes, jboolean enable_gzip_decompression,
    jboolean enable_brotli_decompression, jlong num_timeouts_to_trigger_port_migration,
    jboolean enable_socket_tagging, jboolean enable_interface_binding,
    jlong h2_connection_keepalive_idle_interval_milliseconds,
    jlong h2_connection_keepalive_timeout_seconds, jlong max_connections_per_host,
    jlong stream_idle_timeout_seconds, jlong per_try_idle_timeout_seconds, jstring app_version,
    jstring app_id, jboolean trust_chain_verification, jobjectArray filter_chain,
    jboolean enable_platform_certificates_validation, jstring upstream_tls_sni,
    jobjectArray runtime_guards, jobjectArray cares_fallback_resolvers) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::Platform::EngineBuilder builder;

  configureBuilder(jni_helper, connect_timeout_seconds, dns_refresh_seconds,
                   dns_failure_refresh_seconds_base, dns_failure_refresh_seconds_max,
                   dns_query_timeout_seconds, dns_min_refresh_seconds, dns_preresolve_hostnames,
                   enable_dns_cache, dns_cache_save_interval_seconds, dns_num_retries,
                   enable_drain_post_dns_refresh, enable_http3, use_cares, force_v6, use_gro,
                   http3_connection_options, http3_client_connection_options, quic_hints,
                   quic_canonical_suffixes, enable_gzip_decompression, enable_brotli_decompression,
                   num_timeouts_to_trigger_port_migration, enable_socket_tagging,
                   enable_interface_binding, h2_connection_keepalive_idle_interval_milliseconds,
                   h2_connection_keepalive_timeout_seconds, max_connections_per_host,
                   stream_idle_timeout_seconds, per_try_idle_timeout_seconds, app_version, app_id,
                   trust_chain_verification, filter_chain, enable_platform_certificates_validation,
                   upstream_tls_sni, runtime_guards, cares_fallback_resolvers, builder);

  return reinterpret_cast<intptr_t>(builder.generateBootstrap().release());
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern "C" JNIEXPORT jint JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_resetConnectivityState(JNIEnv* /*env*/,
                                                                        jclass, // class
                                                                        jlong engine) {
  return reinterpret_cast<Envoy::InternalEngine*>(engine)->resetConnectivityState();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_onDefaultNetworkAvailable(JNIEnv*, jclass,
                                                                           jlong engine) {
  reinterpret_cast<Envoy::InternalEngine*>(engine)->onDefaultNetworkAvailable();
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_onDefaultNetworkChanged(JNIEnv*, jclass,
                                                                         jlong engine,
                                                                         jint network_type) {
  reinterpret_cast<Envoy::InternalEngine*>(engine)->onDefaultNetworkChanged(
      static_cast<Envoy::NetworkType>(network_type));
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_onDefaultNetworkUnavailable(JNIEnv*, jclass,
                                                                             jlong engine) {
  reinterpret_cast<Envoy::InternalEngine*>(engine)->onDefaultNetworkUnavailable();
}

extern "C" JNIEXPORT jint JNICALL Java_io_envoyproxy_envoymobile_engine_JniLibrary_setProxySettings(
    JNIEnv* env,
    jclass, // class
    jlong engine, jstring host, jint port) {
  Envoy::JNI::JniHelper jni_helper(env);
  Envoy::JNI::StringUtfUniquePtr java_host = jni_helper.getStringUtfChars(host, nullptr);
  const uint16_t native_port = static_cast<uint16_t>(port);

  envoy_status_t result = reinterpret_cast<Envoy::InternalEngine*>(engine)->setProxySettings(
      java_host.get(), native_port);

  return result;
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
    JNIEnv* env, jclass, jbyteArray java_cert) {
  Envoy::JNI::JniHelper jni_helper(env);
  std::vector<uint8_t> cpp_cert;
  Envoy::JNI::javaByteArrayToByteVector(jni_helper, java_cert, &cpp_cert);
  jclass java_android_network_library_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary");
  jmethodID java_add_test_root_certificate_method_id = jni_helper.getStaticMethodIdFromCache(
      java_android_network_library_class, "addTestRootCertificate", "([B)V");
  Envoy::JNI::LocalRefUniquePtr<jbyteArray> cert_array =
      Envoy::JNI::byteArrayToJavaByteArray(jni_helper, cpp_cert.data(), cpp_cert.size());
  jni_helper.callStaticVoidMethod(java_android_network_library_class,
                                  java_add_test_root_certificate_method_id, cert_array.get());
}

extern "C" JNIEXPORT void JNICALL
Java_io_envoyproxy_envoymobile_engine_JniLibrary_callClearTestRootCertificateFromNative(JNIEnv*,
                                                                                        jclass) {
  Envoy::JNI::JniHelper jni_helper(Envoy::JNI::JniHelper::getThreadLocalEnv());
  jclass java_android_network_library_class =
      jni_helper.findClassFromCache("io/envoyproxy/envoymobile/utilities/AndroidNetworkLibrary");
  jmethodID java_clear_test_root_certificates_method_id = jni_helper.getStaticMethodIdFromCache(
      java_android_network_library_class, "clearTestRootCertificates", "()V");
  jni_helper.callStaticVoidMethod(java_android_network_library_class,
                                  java_clear_test_root_certificates_method_id);
}
