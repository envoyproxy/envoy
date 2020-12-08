#import "library/objective-c/EnvoyEngine.h"
#import "library/objective-c/EnvoyBridgeUtility.h"
#import "library/objective-c/EnvoyHTTPFilterCallbacksImpl.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

#import <UIKit/UIKit.h>

static void ios_on_engine_running(void *context) {
  EnvoyEngineImpl *engineImpl = (__bridge EnvoyEngineImpl *)context;
  if (engineImpl.onEngineRunning) {
    engineImpl.onEngineRunning();
  }
}

static void ios_on_exit(void *context) {
  // Currently nothing needs to happen in iOS on exit. Just log.
  NSLog(@"[Envoy] library is exiting");
}

static const void *ios_http_filter_init(const void *context) {
  EnvoyHTTPFilterFactory *filterFactory = (__bridge EnvoyHTTPFilterFactory *)context;
  EnvoyHTTPFilter *filter = filterFactory.create();
  return CFBridgingRetain(filter);
}

static envoy_filter_headers_status
ios_http_filter_on_request_headers(envoy_headers headers, bool end_stream, const void *context) {
  // TODO(goaway): optimize unmodified case
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onRequestHeaders == nil) {
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusContinue,
                                         /*headers*/ headers};
  }

  EnvoyHeaders *platformHeaders = to_ios_headers(headers);
  // TODO(goaway): consider better solution for compound return
  NSArray *result = filter.onRequestHeaders(platformHeaders, end_stream);
  return (envoy_filter_headers_status){/*status*/ [result[0] intValue],
                                       /*headers*/ toNativeHeaders(result[1])};
}

static envoy_filter_headers_status
ios_http_filter_on_response_headers(envoy_headers headers, bool end_stream, const void *context) {
  // TODO(goaway): optimize unmodified case
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onResponseHeaders == nil) {
    return (envoy_filter_headers_status){/*status*/ kEnvoyFilterHeadersStatusContinue,
                                         /*headers*/ headers};
  }

  EnvoyHeaders *platformHeaders = to_ios_headers(headers);
  NSArray *result = filter.onResponseHeaders(platformHeaders, end_stream);
  return (envoy_filter_headers_status){/*status*/ [result[0] intValue],
                                       /*headers*/ toNativeHeaders(result[1])};
}

static envoy_filter_data_status ios_http_filter_on_request_data(envoy_data data, bool end_stream,
                                                                const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onRequestData == nil) {
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterDataStatusContinue,
                                      /*data*/ data,
                                      /*pending_headers*/ NULL};
  }

  NSData *platformData = to_ios_data(data);
  NSArray *result = filter.onRequestData(platformData, end_stream);
  // Result is typically a pair of status and entity, but uniquely in the case of
  // ResumeIteration it will (optionally) contain additional pending elements.
  envoy_headers *pending_headers = toNativeHeadersPtr(result.count == 3 ? result[2] : nil);
  return (envoy_filter_data_status){/*status*/ [result[0] intValue],
                                    /*data*/ toNativeData(result[1]),
                                    /*pending_headers*/ pending_headers};
}

static envoy_filter_data_status ios_http_filter_on_response_data(envoy_data data, bool end_stream,
                                                                 const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onResponseData == nil) {
    return (envoy_filter_data_status){/*status*/ kEnvoyFilterDataStatusContinue,
                                      /*data*/ data,
                                      /*pending_headers*/ NULL};
  }

  NSData *platformData = to_ios_data(data);
  NSArray *result = filter.onResponseData(platformData, end_stream);
  // Result is typically a pair of status and entity, but uniquely in the case of
  // ResumeIteration it will (optionally) contain additional pending elements.
  envoy_headers *pending_headers = toNativeHeadersPtr(result.count == 3 ? result[2] : nil);
  return (envoy_filter_data_status){/*status*/ [result[0] intValue],
                                    /*data*/ toNativeData(result[1]),
                                    /*pending_headers*/ pending_headers};
}

static envoy_filter_trailers_status ios_http_filter_on_request_trailers(envoy_headers trailers,
                                                                        const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onRequestTrailers == nil) {
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterTrailersStatusContinue,
                                          /*trailers*/ trailers,
                                          /*pending_headers*/ NULL,
                                          /*pending_trailers*/ NULL};
  }

  EnvoyHeaders *platformTrailers = to_ios_headers(trailers);
  NSArray *result = filter.onRequestTrailers(platformTrailers);
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

static envoy_filter_trailers_status ios_http_filter_on_response_trailers(envoy_headers trailers,
                                                                         const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.onResponseTrailers == nil) {
    return (envoy_filter_trailers_status){/*status*/ kEnvoyFilterTrailersStatusContinue,
                                          /*trailers*/ trailers,
                                          /*pending_headers*/ NULL,
                                          /*pending_data*/ NULL};
  }

  EnvoyHeaders *platformTrailers = to_ios_headers(trailers);
  NSArray *result = filter.onResponseTrailers(platformTrailers);
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

static envoy_filter_resume_status
ios_http_filter_on_resume_request(envoy_headers *headers, envoy_data *data, envoy_headers *trailers,
                                  bool end_stream, const void *context) {
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
  NSArray *result =
      filter.onResumeRequest(pendingHeaders, pendingData, pendingTrailers, end_stream);
  return (envoy_filter_resume_status){/*status*/ [result[0] intValue],
                                      /*pending_headers*/ toNativeHeadersPtr(result[1]),
                                      /*pending_data*/ toNativeDataPtr(result[2]),
                                      /*pending_trailers*/ toNativeHeadersPtr(result[3])};
}

static envoy_filter_resume_status
ios_http_filter_on_resume_response(envoy_headers *headers, envoy_data *data,
                                   envoy_headers *trailers, bool end_stream, const void *context) {
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
  NSArray *result =
      filter.onResumeResponse(pendingHeaders, pendingData, pendingTrailers, end_stream);
  return (envoy_filter_resume_status){/*status*/ [result[0] intValue],
                                      /*pending_headers*/ toNativeHeadersPtr(result[1]),
                                      /*pending_data*/ toNativeDataPtr(result[2]),
                                      /*pending_trailers*/ toNativeHeadersPtr(result[3])};
}

static void ios_http_filter_set_request_callbacks(envoy_http_filter_callbacks callbacks,
                                                  const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.setRequestFilterCallbacks == nil) {
    return;
  }

  EnvoyHTTPFilterCallbacksImpl *requestFilterCallbacks =
      [[EnvoyHTTPFilterCallbacksImpl alloc] initWithCallbacks:callbacks];
  filter.setRequestFilterCallbacks(requestFilterCallbacks);
}

static void ios_http_filter_set_response_callbacks(envoy_http_filter_callbacks callbacks,
                                                   const void *context) {
  EnvoyHTTPFilter *filter = (__bridge EnvoyHTTPFilter *)context;
  if (filter.setResponseFilterCallbacks == nil) {
    return;
  }

  EnvoyHTTPFilterCallbacksImpl *responseFilterCallbacks =
      [[EnvoyHTTPFilterCallbacksImpl alloc] initWithCallbacks:callbacks];
  filter.setResponseFilterCallbacks(responseFilterCallbacks);
}

static void ios_http_filter_release(const void *context) {
  CFRelease(context);
  return;
}

@implementation EnvoyEngineImpl {
  envoy_engine_t _engineHandle;
}

- (instancetype)init {
  self = [super init];
  if (!self) {
    return nil;
  }

  _engineHandle = init_engine();
  [EnvoyNetworkMonitor startReachabilityIfNeeded];
  return self;
}

- (void)dealloc {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (int)registerFilterFactory:(EnvoyHTTPFilterFactory *)filterFactory {
  // TODO(goaway): Everything here leaks, but it's all be tied to the life of the engine.
  // This will need to be updated for https://github.com/lyft/envoy-mobile/issues/332
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
  api->release_filter = ios_http_filter_release;
  api->static_context = CFBridgingRetain(filterFactory);
  api->instance_context = NULL;

  register_platform_api(filterFactory.filterName.UTF8String, api);
  return kEnvoySuccess;
}

- (int)runWithConfig:(EnvoyConfiguration *)config
            logLevel:(NSString *)logLevel
     onEngineRunning:(nullable void (^)())onEngineRunning {
  NSString *templateYAML = [[NSString alloc] initWithUTF8String:config_template];
  NSString *resolvedYAML = [config resolveTemplate:templateYAML];
  if (resolvedYAML == nil) {
    return kEnvoyFailure;
  }

  for (EnvoyHTTPFilterFactory *filterFactory in config.httpPlatformFilterFactories) {
    [self registerFilterFactory:filterFactory];
  }

  return [self runWithConfigYAML:resolvedYAML logLevel:logLevel onEngineRunning:onEngineRunning];
}

- (int)runWithTemplate:(NSString *)yaml
                config:(EnvoyConfiguration *)config
              logLevel:(NSString *)logLevel
       onEngineRunning:(nullable void (^)())onEngineRunning {
  NSString *resolvedYAML = [config resolveTemplate:yaml];
  if (resolvedYAML == nil) {
    return kEnvoyFailure;
  }

  for (EnvoyHTTPFilterFactory *filterFactory in config.httpPlatformFilterFactories) {
    [self registerFilterFactory:filterFactory];
  }

  return [self runWithConfigYAML:resolvedYAML logLevel:logLevel onEngineRunning:onEngineRunning];
}

- (int)runWithConfigYAML:(NSString *)configYAML
                logLevel:(NSString *)logLevel
         onEngineRunning:(nullable void (^)())onEngineRunning {
  self.onEngineRunning = onEngineRunning;
  [self startObservingLifecycleNotifications];

  // Envoy exceptions will only be caught here when compiled for 64-bit arches.
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Exceptions/Articles/Exceptions64Bit.html
  @try {
    envoy_engine_callbacks native_callbacks = {ios_on_engine_running, ios_on_exit,
                                               (__bridge void *)(self)};
    return (int)run_engine(_engineHandle, native_callbacks, configYAML.UTF8String,
                           logLevel.UTF8String);
  } @catch (NSException *exception) {
    NSLog(@"[Envoy] exception caught: %@", exception);
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyError" object:self];
    return kEnvoyFailure;
  }
}

- (id<EnvoyHTTPStream>)startStreamWithCallbacks:(EnvoyHTTPCallbacks *)callbacks {
  return [[EnvoyHTTPStreamImpl alloc] initWithHandle:init_stream(_engineHandle)
                                           callbacks:callbacks];
}

- (int)recordCounterInc:(NSString *)elements count:(NSUInteger)count {
  return record_counter_inc(_engineHandle, elements.UTF8String, count);
}

- (int)recordGaugeSet:(NSString *)elements value:(NSUInteger)value {
  return record_gauge_set(_engineHandle, elements.UTF8String, value);
}

- (int)recordGaugeAdd:(NSString *)elements amount:(NSUInteger)amount {
  return record_gauge_add(_engineHandle, elements.UTF8String, amount);
}

- (int)recordGaugeSub:(NSString *)elements amount:(NSUInteger)amount {
  return record_gauge_sub(_engineHandle, elements.UTF8String, amount);
}

#pragma mark - Private

- (void)startObservingLifecycleNotifications {
  // re-enable lifecycle-based stat flushing when https://github.com/lyft/envoy-mobile/issues/748
  // gets fixed.
  NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(terminateNotification:)
                             name:UIApplicationWillTerminateNotification
                           object:nil];
}

- (void)terminateNotification:(NSNotification *)notification {
  NSLog(@"[Envoy %ld] terminating engine (%@)", _engineHandle, notification.name);
  terminate_engine(_engineHandle);
}

@end
