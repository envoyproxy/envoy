#import "library/objective-c/EnvoyHTTPFilterCallbacksImpl.h"

#pragma mark - EnvoyHTTPFilterCallbacksImpl

@implementation EnvoyHTTPFilterCallbacksImpl {
  envoy_http_filter_callbacks _callbacks;
}

- (instancetype)initWithCallbacks:(envoy_http_filter_callbacks)callbacks {
  self = [super init];
  if (!self) {
    return nil;
  }

  _callbacks = callbacks;
  return self;
}

- (void)resumeIteration {
  _callbacks.resume_iteration(_callbacks.callback_context);
}

- (void)dealloc {
  _callbacks.release_callbacks(_callbacks.callback_context);
}

@end
