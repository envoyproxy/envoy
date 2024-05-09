#import "library/objective-c/EnvoyEngine.h"
#import "library/objective-c/EnvoyBridgeUtility.h"
#import "library/objective-c/EnvoyHTTPFilterCallbacksImpl.h"
#import "library/objective-c/EnvoyKeyValueStoreBridgeImpl.h"

#include "library/common/api/c_types.h"

#import "library/common/types/c_types.h"
#import "library/common/extensions/key_value/platform/c_types.h"
#import "library/cc/engine_builder.h"
#import "library/common/internal_engine.h"

#include "library/common/network/apple_proxy_resolution.h"

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

@interface EnvoyConfiguration (CXX)
- (std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>)generateBootstrap;
@end

static const void *ios_http_filter_init(const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    envoy_http_filter *c_filter = (envoy_http_filter *)context;

    EnvoyHTTPFilterFactory *filterFactory =
        (__bridge EnvoyHTTPFilterFactory *)c_filter->static_context;
    EnvoyHTTPFilter *filter = filterFactory.create();

    // Unset static functions on the c_struct based on the created filter
    if (filter.onRequestHeaders == nil) {
      c_filter->on_request_headers = NULL;
    }
    if (filter.onRequestData == nil) {
      c_filter->on_request_data = NULL;
    }
    if (filter.onRequestTrailers == nil) {
      c_filter->on_request_trailers = NULL;
    }

    if (filter.onResponseHeaders == nil) {
      c_filter->on_response_headers = NULL;
    }
    if (filter.onResponseData == nil) {
      c_filter->on_response_data = NULL;
    }
    if (filter.onResponseTrailers == nil) {
      c_filter->on_response_trailers = NULL;
    }

    if (filter.setRequestFilterCallbacks == nil) {
      c_filter->set_request_callbacks = NULL;
    }
    if (filter.onResumeRequest == nil) {
      c_filter->on_resume_request = NULL;
    }

    if (filter.setResponseFilterCallbacks == nil) {
      c_filter->set_response_callbacks = NULL;
    }
    if (filter.onResumeResponse == nil) {
      c_filter->on_resume_response = NULL;
    }

    if (filter.onCancel == nil) {
      c_filter->on_cancel = NULL;
    }
    if (filter.onError == nil) {
      c_filter->on_error = NULL;
    }

    return CFBridgingRetain(filter);
  }
}

static envoy_filter_headers_status
ios_http_filter_on_request_headers(envoy_headers headers, bool end_stream,
                                   envoy_stream_intel stream_intel, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    // TODO(goaway): optimize unmodified case
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onRequestHeaders == nil) {
      return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusContinue,
                                           /*headers*/ headers};
    }

    EnvoyHeaders *platformHeaders = to_ios_headers(headers);
    // TODO(goaway): consider better solution for compound return
    NSArray *result = filter.onRequestHeaders(platformHeaders, end_stream, stream_intel);
    return (envoy_filter_headers_status){/*status*/ [result[0] intValue],
                                         /*headers*/ toNativeHeaders(result[1])};
  }
}

static envoy_filter_headers_status
ios_http_filter_on_response_headers(envoy_headers headers, bool end_stream,
                                    envoy_stream_intel stream_intel, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    // TODO(goaway): optimize unmodified case
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onResponseHeaders == nil) {
      return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusContinue,
                                           /*headers*/ headers};
    }

    EnvoyHeaders *platformHeaders = to_ios_headers(headers);
    NSArray *result = filter.onResponseHeaders(platformHeaders, end_stream, stream_intel);
    return (envoy_filter_headers_status){/*status*/ [result[0] intValue],
                                         /*headers*/ toNativeHeaders(result[1])};
  }
}

static envoy_filter_data_status ios_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                envoy_stream_intel stream_intel,
                                                                const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onRequestData == nil) {
      return (envoy_filter_data_status){/*status*/ kEnvoyFilterDataStatusContinue,
                                        /*data*/ data,
                                        /*pending_headers*/ NULL};
    }

    NSData *platformData = to_ios_data(data);
    NSArray *result = filter.onRequestData(platformData, end_stream, stream_intel);
    // Result is typically a pair of status and entity, but uniquely in the case of
    // ResumeIteration it will (optionally) contain additional pending elements.
    envoy_headers *pending_headers = toNativeHeadersPtr(result.count == 3 ? result[2] : nil);
    return (envoy_filter_data_status){/*status*/ [result[0] intValue],
                                      /*data*/ toNativeData(result[1]),
                                      /*pending_headers*/ pending_headers};
  }
}

static envoy_filter_data_status ios_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 envoy_stream_intel stream_intel,
                                                                 const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onResponseData == nil) {
      return (envoy_filter_data_status){/*status*/ kEnvoyFilterDataStatusContinue,
                                        /*data*/ data,
                                        /*pending_headers*/ NULL};
    }

    NSData *platformData = to_ios_data(data);
    NSArray *result = filter.onResponseData(platformData, end_stream, stream_intel);
    // Result is typically a pair of status and entity, but uniquely in the case of
    // ResumeIteration it will (optionally) contain additional pending elements.
    envoy_headers *pending_headers = toNativeHeadersPtr(result.count == 3 ? result[2] : nil);
    return (envoy_filter_data_status){/*status*/ [result[0] intValue],
                                      /*data*/ toNativeData(result[1]),
                                      /*pending_headers*/ pending_headers};
  }
}

static envoy_filter_trailers_status
ios_http_filter_on_request_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                    const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onRequestTrailers == nil) {
      return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterTrailersStatusContinue,
                                            /*trailers*/ trailers,
                                            /*pending_headers*/ NULL,
                                            /*pending_trailers*/ NULL};
    }

    EnvoyHeaders *platformTrailers = to_ios_headers(trailers);
    NSArray *result = filter.onRequestTrailers(platformTrailers, stream_intel);
    envoy_headers *pending_headers = NULL;
    envoy_data *pending_data = NULL;
    // Result is typically a pair of status and entity, but uniquely in the case of
    // ResumeIteration it will (optionally) contain additional pending elements.
    if (result.count == 4) {
      pending_headers = toNativeHeadersPtr(result[2]);
      pending_data = toNativeDataPtr(result[3]);
    }
    return (envoy_filter_trailers_status){/*status*/ [result[0] intValue],
                                          /*trailers*/ toNativeHeaders(result[1]),
                                          /*pending_headers*/ pending_headers,
                                          /*pending_data*/ pending_data};
  }
}

static envoy_filter_trailers_status
ios_http_filter_on_response_trailers(envoy_headers trailers, envoy_stream_intel stream_intel,
                                     const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onResponseTrailers == nil) {
      return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterTrailersStatusContinue,
                                            /*trailers*/ trailers,
                                            /*pending_headers*/ NULL,
                                            /*pending_data*/ NULL};
    }

    EnvoyHeaders *platformTrailers = to_ios_headers(trailers);
    NSArray *result = filter.onResponseTrailers(platformTrailers, stream_intel);
    envoy_headers *pending_headers = NULL;
    envoy_data *pending_data = NULL;
    // Result is typically a pair of status and entity, but uniquely in the case of
    // ResumeIteration it will (optionally) contain additional pending elements.
    if (result.count == 4) {
      pending_headers = toNativeHeadersPtr(result[2]);
      pending_data = toNativeDataPtr(result[3]);
    }
    return (envoy_filter_trailers_status){/*status*/ [result[0] intValue],
                                          /*trailers*/ toNativeHeaders(result[1]),
                                          /*pending_headers*/ pending_headers,
                                          /*pending_data*/ pending_data};
  }
}

static envoy_filter_resume_status
ios_http_filter_on_resume_request(envoy_headers *headers, envoy_data *data, envoy_headers *trailers,
                                  bool end_stream, envoy_stream_intel stream_intel,
                                  const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onResumeRequest == nil) {
      return (envoy_filter_resume_status){/*status*/ kEnvoyFilterResumeStatusResumeIteration,
                                          /*pending_headers*/ headers,
                                          /*pending_data*/ data,
                                          /*pending_trailers*/ trailers};
    }

    EnvoyHeaders *pendingHeaders = headers ? to_ios_headers(*headers) : nil;
    NSData *pendingData = data ? to_ios_data(*data) : nil;
    EnvoyHeaders *pendingTrailers = trailers ? to_ios_headers(*trailers) : nil;
    NSArray *result = filter.onResumeRequest(pendingHeaders, pendingData, pendingTrailers,
                                             end_stream, stream_intel);
    return (envoy_filter_resume_status){/*status*/ [result[0] intValue],
                                        /*pending_headers*/ toNativeHeadersPtr(result[1]),
                                        /*pending_data*/ toNativeDataPtr(result[2]),
                                        /*pending_trailers*/ toNativeHeadersPtr(result[3])};
  }
}

static envoy_filter_resume_status
ios_http_filter_on_resume_response(envoy_headers *headers, envoy_data *data,
                                   envoy_headers *trailers, bool end_stream,
                                   envoy_stream_intel stream_intel, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onResumeResponse == nil) {
      return (envoy_filter_resume_status){/*status*/ kEnvoyFilterResumeStatusResumeIteration,
                                          /*pending_headers*/ headers,
                                          /*pending_data*/ data,
                                          /*pending_trailers*/ trailers};
    }

    EnvoyHeaders *pendingHeaders = headers ? to_ios_headers(*headers) : nil;
    NSData *pendingData = data ? to_ios_data(*data) : nil;
    EnvoyHeaders *pendingTrailers = trailers ? to_ios_headers(*trailers) : nil;
    NSArray *result = filter.onResumeResponse(pendingHeaders, pendingData, pendingTrailers,
                                              end_stream, stream_intel);
    return (envoy_filter_resume_status){/*status*/ [result[0] intValue],
                                        /*pending_headers*/ toNativeHeadersPtr(result[1]),
                                        /*pending_data*/ toNativeDataPtr(result[2]),
                                        /*pending_trailers*/ toNativeHeadersPtr(result[3])};
  }
}

static void ios_http_filter_set_request_callbacks(envoy_http_filter_callbacks callbacks,
                                                  const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.setRequestFilterCallbacks == nil) {
      return;
    }

    EnvoyHTTPFilterCallbacksImpl *requestFilterCallbacks =
        [[EnvoyHTTPFilterCallbacksImpl alloc] initWithCallbacks:callbacks];
    filter.setRequestFilterCallbacks(requestFilterCallbacks);
  }
}

static void ios_http_filter_set_response_callbacks(envoy_http_filter_callbacks callbacks,
                                                   const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.setResponseFilterCallbacks == nil) {
      return;
    }

    EnvoyHTTPFilterCallbacksImpl *responseFilterCallbacks =
        [[EnvoyHTTPFilterCallbacksImpl alloc] initWithCallbacks:callbacks];
    filter.setResponseFilterCallbacks(responseFilterCallbacks);
  }
}

static void ios_http_filter_on_cancel(envoy_stream_intel stream_intel,
                                      envoy_final_stream_intel final_stream_intel,
                                      const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onCancel == nil) {
      return;
    }
    filter.onCancel(stream_intel, final_stream_intel);
  }
}

static void ios_http_filter_on_error(envoy_error error, envoy_stream_intel stream_intel,
                                     envoy_final_stream_intel final_stream_intel,
                                     const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onError == nil) {
      release_envoy_error(error);
      return;
    }

    NSString *errorMessage = [[NSString alloc] initWithBytes:error.message.bytes
                                                      length:error.message.length
                                                    encoding:NSUTF8StringEncoding];

    release_envoy_error(error);
    filter.onError(error.error_code, errorMessage, error.attempt_count, stream_intel,
                   final_stream_intel);
  }
}

static void ios_http_filter_release(const void *context) {
  CFRelease(context);
  return;
}

static envoy_data ios_get_string(const void *context) {
  EnvoyStringAccessor *accessor = (__bridge EnvoyStringAccessor *)context;
  return toManagedNativeString(accessor.getEnvoyString());
}

@implementation EnvoyEngineImpl {
  envoy_engine_t _engineHandle;
  Envoy::InternalEngine *_engine;
  EnvoyNetworkMonitor *_networkMonitor;
}

- (instancetype)initWithRunningCallback:(nullable void (^)())onEngineRunning
                                 logger:(nullable void (^)(NSInteger, NSString *))logger
                           eventTracker:(nullable void (^)(EnvoyEvent *))eventTracker
                  networkMonitoringMode:(int)networkMonitoringMode {
  self = [super init];
  if (!self) {
    return nil;
  }

  std::unique_ptr<Envoy::EngineCallbacks> native_callbacks =
      std::make_unique<Envoy::EngineCallbacks>();
  native_callbacks->on_engine_running_ = [onEngineRunning = std::move(onEngineRunning)] {
    // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
    // block is necessary to act as a breaker for any Objective-C allocation that happens.
    @autoreleasepool {
      if (onEngineRunning) {
        onEngineRunning();
      }
    }
  };
  native_callbacks->on_exit_ = [] {
    // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
    // block is necessary to act as a breaker for any Objective-C allocation that happens.
    @autoreleasepool {
      NSLog(@"[Envoy] library is exiting");
    }
  };

  std::unique_ptr<Envoy::EnvoyLogger> native_logger = std::make_unique<Envoy::EnvoyLogger>();
  if (logger) {
    native_logger->on_log_ = [logger = std::move(logger)](Envoy::Logger::Logger::Levels level,
                                                          const std::string &message) {
      // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool
      // block is necessary to act as a breaker for any Objective-C allocation that happens.
      @autoreleasepool {
        logger(level, @(message.c_str()));
      }
    };
  }

  std::unique_ptr<Envoy::EnvoyEventTracker> native_event_tracker =
      std::make_unique<Envoy::EnvoyEventTracker>();
  if (eventTracker) {
    native_event_tracker->on_track_ =
        [eventTracker =
             std::move(eventTracker)](const absl::flat_hash_map<std::string, std::string> &events) {
          NSMutableDictionary *newMap = [NSMutableDictionary new];
          for (const auto &[cppKey, cppValue] : events) {
            NSString *key = @(cppKey.c_str());
            NSString *value = @(cppValue.c_str());
            newMap[key] = value;
          }
          eventTracker(newMap);
        };
  }

  _engine = new Envoy::InternalEngine(std::move(native_callbacks), std::move(native_logger),
                                      std::move(native_event_tracker));
  _engineHandle = reinterpret_cast<envoy_engine_t>(_engine);

  if (networkMonitoringMode == 1) {
    [_networkMonitor startReachability];
  } else if (networkMonitoringMode == 2) {
    [_networkMonitor startPathMonitor];
  }

  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (int)registerFilterFactory:(EnvoyHTTPFilterFactory *)filterFactory {
  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  envoy_http_filter *api = (envoy_http_filter *)safe_malloc(sizeof(envoy_http_filter));
  api->init_filter = ios_http_filter_init;
  api->on_request_headers = ios_http_filter_on_request_headers;
  api->on_request_data = ios_http_filter_on_request_data;
  api->on_request_trailers = ios_http_filter_on_request_trailers;
  api->on_response_headers = ios_http_filter_on_response_headers;
  api->on_response_data = ios_http_filter_on_response_data;
  api->on_response_trailers = ios_http_filter_on_response_trailers;
  api->set_request_callbacks = ios_http_filter_set_request_callbacks;
  api->on_resume_request = ios_http_filter_on_resume_request;
  api->set_response_callbacks = ios_http_filter_set_response_callbacks;
  api->on_resume_response = ios_http_filter_on_resume_response;
  // TODO(goaway) HTTP filter on_complete not currently implemented.
  // api->on_complete = ios_http_filter_on_complete;
  api->on_cancel = ios_http_filter_on_cancel;
  api->on_error = ios_http_filter_on_error;
  api->release_filter = ios_http_filter_release;
  api->static_context = CFBridgingRetain(filterFactory);
  api->instance_context = NULL;

  Envoy::Api::External::registerApi(filterFactory.filterName.UTF8String, api);
  return kEnvoySuccess;
}

- (int)registerStringAccessor:(NSString *)name accessor:(EnvoyStringAccessor *)accessor {
  // TODO(goaway): Everything here leaks, but it's all tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  envoy_string_accessor *accessorStruct =
      (envoy_string_accessor *)safe_malloc(sizeof(envoy_string_accessor));
  accessorStruct->get_string = ios_get_string;
  accessorStruct->context = CFBridgingRetain(accessor);

  Envoy::Api::External::registerApi(name.UTF8String, accessorStruct);
  return ENVOY_SUCCESS;
}

- (int)registerKeyValueStore:(NSString *)name keyValueStore:(id<EnvoyKeyValueStore>)keyValueStore {
  envoy_kv_store *api = (envoy_kv_store *)safe_malloc(sizeof(envoy_kv_store));
  api->save = ios_kv_store_save;
  api->read = ios_kv_store_read;
  api->remove = ios_kv_store_remove;
  api->context = CFBridgingRetain(keyValueStore);

  Envoy::Api::External::registerApi(name.UTF8String, api);
  return ENVOY_SUCCESS;
}

- (void)performRegistrationsForConfig:(EnvoyConfiguration *)config {
  for (EnvoyHTTPFilterFactory *filterFactory in config.httpPlatformFilterFactories) {
    [self registerFilterFactory:filterFactory];
  }

  for (NSString *name in config.stringAccessors) {
    [self registerStringAccessor:name accessor:config.stringAccessors[name]];
  }

  for (NSString *name in config.keyValueStores) {
    [self registerKeyValueStore:name keyValueStore:config.keyValueStores[name]];
  }

  if (config.respectSystemProxySettings) {
    registerAppleProxyResolver();
  }
}

- (int)runWithConfig:(EnvoyConfiguration *)config logLevel:(NSString *)logLevel {
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap;
  if (config.bootstrapPointer > 0) {
    bootstrap = absl::WrapUnique(
        reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap *>(config.bootstrapPointer));
  } else {
    bootstrap = [config generateBootstrap];
  }

  if (bootstrap == nullptr) {
    return kEnvoyFailure;
  }

  [self performRegistrationsForConfig:config];
  [self startObservingLifecycleNotifications];

  @try {
    auto options = std::make_unique<Envoy::OptionsImplBase>();
    options->setConfigProto(std::move(bootstrap));
    ENVOY_BUG(options->setLogLevel(logLevel.UTF8String).ok(), "invalid log level");
    options->setConcurrency(1);
    return _engine->run(std::move(options));
  } @catch (NSException *exception) {
    [self logException:exception];
    return kEnvoyFailure;
  }
}

- (id<EnvoyHTTPStream>)startStreamWithCallbacks:(EnvoyHTTPCallbacks *)callbacks
                            explicitFlowControl:(BOOL)explicitFlowControl {
  return [[EnvoyHTTPStreamImpl alloc] initWithHandle:_engine->initStream()
                                              engine:reinterpret_cast<envoy_engine_t>(_engine)
                                           callbacks:callbacks
                                 explicitFlowControl:explicitFlowControl];
}

- (int)recordCounterInc:(NSString *)elements tags:(EnvoyTags *)tags count:(NSUInteger)count {
  // TODO: update to use real tag array when the API layer change is ready.
  return _engine->recordCounterInc(elements.UTF8String, toNativeStatsTags(tags), count);
}

- (NSString *)dumpStats {
  std::string status = _engine->dumpStats();
  if (status == "") {
    return @"";
  }

  return @(status.c_str());
}

- (void)terminate {
  _engine->terminate();
}

- (void)resetConnectivityState {
  _engine->resetConnectivityState();
}

#pragma mark - Private

- (void)startObservingLifecycleNotifications {
#if TARGET_OS_IPHONE
  // re-enable lifecycle-based stat flushing when
  // https://github.com/envoyproxy/envoy-mobile/issues/748 gets fixed.
  NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(terminateNotification:)
                             name:UIApplicationWillTerminateNotification
                           object:nil];
#endif
}

- (void)terminateNotification:(NSNotification *)notification {
  NSLog(@"[Envoy %ld] terminating engine (%@)", _engineHandle, notification.name);
  _engine->terminate();
}

- (void)logException:(NSException *)exception {
  NSLog(@"[Envoy] exception caught: %@", exception);

  NSString *message = [NSString stringWithFormat:@"%@;%@;%@", exception.name, exception.reason,
                                                 exception.callStackSymbols.description];
  ENVOY_LOG_EVENT_TO_LOGGER(Envoy::Logger::Registry::getLog(Envoy::Logger::Id::misc), error,
                            "handled_cxx_exception", [message UTF8String]);

  [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyHandledCXXException"
                                                    object:exception];
}

@end
