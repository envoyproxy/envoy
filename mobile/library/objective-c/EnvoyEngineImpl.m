#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"
#import "library/common/types/c_types.h"

@implementation EnvoyEngineImpl {
  envoy_engine_t _engineHandle;
}

- (instancetype)init {
  self = [super init];
  if (!self) {
    return nil;
  }
  _engineHandle = init_engine();
  return self;
}

- (int)runWithConfig:(NSString *)config {
  return [self runWithConfig:config logLevel:@"info"];
}

- (int)runWithConfig:(NSString *)config logLevel:(NSString *)logLevel {
  // Envoy exceptions will only be caught here when compiled for 64-bit arches.
  // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Exceptions/Articles/Exceptions64Bit.html
  @try {
    return (int)run_engine(config.UTF8String, logLevel.UTF8String);
  } @catch (...) {
    NSLog(@"Envoy exception caught.");
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyException" object:self];
    return 1;
  }
}

- (EnvoyHTTPStream *)startStreamWithObserver:(EnvoyObserver *)observer {
  return [[EnvoyHTTPStream alloc] initWithHandle:init_stream(_engineHandle) observer:observer];
}

@end
