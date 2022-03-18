#import "library/objective-c/EnvoyEngine.h"
#import "library/objective-c/EnvoyBridgeUtility.h"
#import "library/objective-c/EnvoyHTTPFilterCallbacksImpl.h"

#include "library/common/api/c_types.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#endif

static void ios_on_engine_running(void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyEngineImpl *engineImpl = (__bridge EnvoyEngineImpl *)context;
    if (engineImpl.onEngineRunning) {
      engineImpl.onEngineRunning();
    }
  }
}

static void ios_on_exit(void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    NSLog(@"[Envoy] library is exiting");
  }
}

static void ios_on_log(envoy_data data, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyLogger *logger = (__bridge EnvoyLogger *)context;
    logger.log(to_ios_string(data));
  }
}

static void ios_on_logger_release(const void *context) { CFRelease(context); }

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

static void ios_http_filter_on_complete(envoy_stream_intel stream_intel,
                                        envoy_final_stream_intel final_stream_intel,
                                        const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
    if (filter.onComplete == nil) {
      return;
    }
    filter.onComplete(stream_intel, final_stream_intel);
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

static void ios_track_event(envoy_map map, const void *context) {
  // This code block runs inside the Envoy event loop. Therefore, an explicit autoreleasepool block
  // is necessary to act as a breaker for any Objective-C allocation that happens.
  @autoreleasepool {
    EnvoyEventTracker *eventTracker = (__bridge EnvoyEventTracker *)context;
    eventTracker.track(to_ios_map(map));
  }
}

@implementation EnvoyEngineImpl {
  envoy_engine_t _engineHandle;
}

- (instancetype)initWithRunningCallback:(nullable void (^)())onEngineRunning
                                 logger:(nullable void (^)(NSString *))logger
                           eventTracker:(nullable void (^)(EnvoyEvent *))eventTracker
               enableNetworkPathMonitor:(BOOL)enableNetworkPathMonitor {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.onEngineRunning = onEngineRunning;
  envoy_engine_callbacks native_callbacks = {ios_on_engine_running, ios_on_exit,
                                             (__bridge void *)(self)};

  envoy_logger native_logger = {NULL, NULL, NULL};
  if (logger) {
    EnvoyLogger *objcLogger = [[EnvoyLogger alloc] initWithLogClosure:logger];
    native_logger.log = ios_on_log;
    native_logger.release = ios_on_logger_release;
    native_logger.context = CFBridgingRetain(objcLogger);
  }

  // TODO(Augustyniak): Everything here leaks, but it's all tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332.
  envoy_event_tracker native_event_tracker = {NULL, NULL};
  if (eventTracker) {
    EnvoyEventTracker *objcEventTracker =
        [[EnvoyEventTracker alloc] initWithEventTrackingClosure:eventTracker];
    native_event_tracker.track = ios_track_event;
    native_event_tracker.context = CFBridgingRetain(objcEventTracker);
  }

  _engineHandle = init_engine(native_callbacks, native_logger, native_event_tracker);

  if (enableNetworkPathMonitor) {
    [EnvoyNetworkMonitor startPathMonitorIfNeeded];
  } else {
    [EnvoyNetworkMonitor startReachabilityIfNeeded];
  }

  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (int)registerFilterFactory:(EnvoyHTTPFilterFactory *)filterFactory {
  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  envoy_http_filter *api = safe_malloc(sizeof(envoy_http_filter));
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

  register_platform_api(filterFactory.filterName.UTF8String, api);
  return kEnvoySuccess;
}

- (int)registerStringAccessor:(NSString *)name accessor:(EnvoyStringAccessor *)accessor {
  // TODO(goaway): Everything here leaks, but it's all tied to the life of the engine.
  // This will need to be updated for https://github.com/envoyproxy/envoy-mobile/issues/332
  envoy_string_accessor *accessorStruct = safe_malloc(sizeof(envoy_string_accessor));
  accessorStruct->get_string = ios_get_string;
  accessorStruct->context = CFBridgingRetain(accessor);

  return register_platform_api(name.UTF8String, accessorStruct);
}

- (int)runWithConfig:(EnvoyConfiguration *)config logLevel:(NSString *)logLevel {
  NSString *templateYAML = [[NSString alloc] initWithUTF8String:config_template];
  return [self runWithTemplate:templateYAML config:config logLevel:logLevel];
}

- (int)runWithTemplate:(NSString *)yaml
                config:(EnvoyConfiguration *)config
              logLevel:(NSString *)logLevel {

  NSString *resolvedYAML = [config resolveTemplate:yaml];
  if (resolvedYAML == nil) {
    return kEnvoyFailure;
  }

  for (EnvoyHTTPFilterFactory *filterFactory in config.httpPlatformFilterFactories) {
    [self registerFilterFactory:filterFactory];
  }

  for (NSString *name in config.stringAccessors) {
    [self registerStringAccessor:name accessor:config.stringAccessors[name]];
  }

  return [self runWithConfigYAML:resolvedYAML logLevel:logLevel];
}

- (int)runWithConfigYAML:(NSString *)configYAML logLevel:(NSString *)logLevel {
  [self startObservingLifecycleNotifications];

  // Envoy exceptions will only be caught here when compiled for 64-bit arches.
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Exceptions/Articles/Exceptions64Bit.html
  @try {
    return (int)run_engine(_engineHandle, configYAML.UTF8String, logLevel.UTF8String);
  } @catch (NSException *exception) {
    NSLog(@"[Envoy] exception caught: %@", exception);
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyError" object:self];
    return kEnvoyFailure;
  }
}

- (id<EnvoyHTTPStream>)startStreamWithCallbacks:(EnvoyHTTPCallbacks *)callbacks
                            explicitFlowControl:(BOOL)explicitFlowControl {
  return [[EnvoyHTTPStreamImpl alloc] initWithHandle:init_stream(_engineHandle)
                                           callbacks:callbacks
                                 explicitFlowControl:explicitFlowControl];
}

- (int)recordCounterInc:(NSString *)elements tags:(EnvoyTags *)tags count:(NSUInteger)count {
  // TODO: update to use real tag array when the API layer change is ready.
  return record_counter_inc(_engineHandle, elements.UTF8String, toNativeStatsTags(tags), count);
}

- (int)recordGaugeSet:(NSString *)elements tags:(EnvoyTags *)tags value:(NSUInteger)value {
  return record_gauge_set(_engineHandle, elements.UTF8String, toNativeStatsTags(tags), value);
}

- (int)recordGaugeAdd:(NSString *)elements tags:(EnvoyTags *)tags amount:(NSUInteger)amount {
  return record_gauge_add(_engineHandle, elements.UTF8String, toNativeStatsTags(tags), amount);
}

- (int)recordGaugeSub:(NSString *)elements tags:(EnvoyTags *)tags amount:(NSUInteger)amount {
  return record_gauge_sub(_engineHandle, elements.UTF8String, toNativeStatsTags(tags), amount);
}

- (int)recordHistogramDuration:(NSString *)elements
                          tags:(EnvoyTags *)tags
                    durationMs:(NSUInteger)durationMs {
  return record_histogram_value(_engineHandle, elements.UTF8String, toNativeStatsTags(tags),
                                durationMs, MILLISECONDS);
}

- (int)recordHistogramValue:(NSString *)elements tags:(EnvoyTags *)tags value:(NSUInteger)value {
  return record_histogram_value(_engineHandle, elements.UTF8String, toNativeStatsTags(tags), value,
                                UNSPECIFIED);
}

- (void)flushStats {
  flush_stats(_engineHandle);
}

- (NSString *)dumpStats {
  envoy_data data;
  envoy_status_t status = dump_stats(_engineHandle, &data);
  if (status != ENVOY_SUCCESS) {
    return @"";
  }

  NSString *stringCopy = [[NSString alloc] initWithBytes:data.bytes
                                                  length:data.length
                                                encoding:NSUTF8StringEncoding];
  release_envoy_data(data);
  return stringCopy;
}

- (void)terminate {
  terminate_engine(_engineHandle);
}

- (void)drainConnections {
  drain_connections(_engineHandle);
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
  terminate_engine(_engineHandle);
}

@end
