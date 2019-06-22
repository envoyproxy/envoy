#import <Foundation/Foundation.h>

@interface Envoy : NSObject

/// Indicates whether this Envoy instance is currently active and running.
@property (nonatomic, readonly) BOOL isRunning;

/// Indicates whether the Envoy instance is terminated.
@property (nonatomic, readonly) BOOL isTerminated;

/// Create a new Envoy instance. The Envoy runner NSThread is started as part of instance
/// initialization with the configuration provided.
- (instancetype)initWithConfig:(NSString*)config;
@end
