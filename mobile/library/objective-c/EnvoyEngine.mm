#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyEngine

+ (EnvoyStatus)runWithConfig:(NSString *)config {
  return [self runWithConfig:config logLevel:@"info"];
}

+ (EnvoyStatus)runWithConfig:(NSString *)config logLevel:(NSString *)logLevel {
  try {
    return (EnvoyStatus)run_engine(config.UTF8String, logLevel.UTF8String);
  } catch (NSException *e) {
    NSLog(@"Envoy exception: %@", e);
    NSDictionary *userInfo = @{@"exception" : e};
    [NSNotificationCenter.defaultCenter postNotificationName:@"EnvoyException"
                                                      object:self
                                                    userInfo:userInfo];
    return Failure;
  }
}

+ (EnvoyStream)startStreamWithObserver:(EnvoyObserver *)observer {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  EnvoyStream stream;
  stream.status = Failure;
  return stream;
}

+ (EnvoyStatus)sendHeaders:(EnvoyHeaders *)headers to:(EnvoyStream *)stream close:(BOOL)close {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

+ (EnvoyStatus)sendData:(NSData *)data to:(EnvoyStream *)stream close:(BOOL)close {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

+ (EnvoyStatus)sendMetadata:(EnvoyHeaders *)metadata to:(EnvoyStream *)stream close:(BOOL)close {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

+ (EnvoyStatus)sendTrailers:(EnvoyHeaders *)trailers to:(EnvoyStream *)stream close:(BOOL)close {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

+ (EnvoyStatus)locallyCloseStream:(EnvoyStream *)stream {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

+ (EnvoyStatus)resetStream:(EnvoyStream *)stream {
  NSLog(@"%@ not implemented, returning failure", NSStringFromSelector((SEL) __func__));
  return Failure;
}

@end
