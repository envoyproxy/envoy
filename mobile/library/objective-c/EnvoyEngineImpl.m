#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

#import <UIKit/UIKit.h>

static void ios_on_exit() {
  // Currently nothing needs to happen in iOS on exit. Just log.
  NSLog(@"[Envoy] library is exiting");
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

- (int)runWithConfig:(EnvoyConfiguration *)config logLevel:(NSString *)logLevel {
  NSString *templateYAML = [[NSString alloc] initWithUTF8String:config_template];
  NSString *resolvedYAML = [config resolveTemplate:templateYAML];
  if (resolvedYAML == nil) {
    return 1;
  }

  return [self runWithConfigYAML:resolvedYAML logLevel:logLevel];
}

- (int)runWithConfigYAML:(NSString *)configYAML logLevel:(NSString *)logLevel {
  [self startObservingLifecycleNotifications];

  // Envoy exceptions will only be caught here when compiled for 64-bit arches.
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Exceptions/Articles/Exceptions64Bit.html
  @try {
    envoy_engine_callbacks native_callbacks = {ios_on_exit};
    return (int)run_engine(_engineHandle, native_callbacks, configYAML.UTF8String,
                           logLevel.UTF8String);
  } @catch (NSException *exception) {
    NSLog(@"[Envoy] exception caught: %@", exception);
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyError" object:self];
    return 1;
  }
}

- (id<EnvoyHTTPStream>)startStreamWithCallbacks:(EnvoyHTTPCallbacks *)callbacks {
  return [[EnvoyHTTPStreamImpl alloc] initWithHandle:init_stream(_engineHandle)
                                           callbacks:callbacks];
}

#pragma mark - Private

- (void)startObservingLifecycleNotifications {
  NSNotificationCenter *notificationCenter = [NSNotificationCenter defaultCenter];
  [notificationCenter addObserver:self
                         selector:@selector(lifecycleDidChangeWithNotification:)
                             name:UIApplicationWillResignActiveNotification
                           object:nil];
  [notificationCenter addObserver:self
                         selector:@selector(lifecycleDidChangeWithNotification:)
                             name:UIApplicationWillTerminateNotification
                           object:nil];
}

- (void)lifecycleDidChangeWithNotification:(NSNotification *)notification {
  NSLog(@"[Envoy] triggering stats flush (%@)", notification.name);
  flush_stats();
}

@end
