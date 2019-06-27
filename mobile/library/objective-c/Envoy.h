#import <Foundation/Foundation.h>

/// Available logging levels for an Envoy instance. Note some levels may be compiled out.
typedef NS_ENUM(NSInteger, EnvoyLogLevel) {
  EnvoyLogLevelTrace,
  EnvoyLogLevelDebug,
  EnvoyLogLevelInfo,
  EnvoyLogLevelWarn,
  EnvoyLogLevelError,
  EnvoyLogLevelCritical,
  EnvoyLogLevelOff
};

@interface Envoy : NSObject

/// Indicates whether this Envoy instance is currently active and running.
@property (nonatomic, readonly) BOOL isRunning;

/// Indicates whether the Envoy instance is terminated.
@property (nonatomic, readonly) BOOL isTerminated;

/// Create a new Envoy instance. The Envoy runner NSThread is started as part of instance
/// initialization with the configuration provided.
- (instancetype)initWithConfig:(NSString *)config;

/// Create a new Envoy instance. The Envoy runner NSThread is started as part of instance
/// initialization with the configuration provided.
- (instancetype)initWithConfig:(NSString *)config logLevel:(EnvoyLogLevel)logLevel;

@end
