#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EMODirectResponse.h"
#import "library/objective-c/EnvoyAliases.h"
#import "library/objective-c/EnvoyConfiguration.h"
#import "library/objective-c/EnvoyEventTracker.h"
#import "library/objective-c/EnvoyHTTPCallbacks.h"
#import "library/objective-c/EnvoyHTTPFilter.h"
#import "library/objective-c/EnvoyHTTPFilterFactory.h"
#import "library/objective-c/EnvoyHTTPStream.h"
#import "library/objective-c/EnvoyKeyValueStore.h"
#import "library/objective-c/EnvoyLogger.h"
#import "library/objective-c/EnvoyNativeFilterConfig.h"
#import "library/objective-c/EnvoyNetworkMonitor.h"
#import "library/objective-c/EnvoyStringAccessor.h"

#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

#pragma mark - EnvoyEngine

/// Wrapper layer for calling into Envoy's C/++ API.
@protocol EnvoyEngine

/**
 Create a new instance of the engine.

 @param onEngineRunning Closure called when the engine finishes its async startup and begins
 running.
 @param logger Logging interface.
 @param eventTracker Event tracking interface.
 @param networkMonitoringMode Configure how the engines observe network reachability.
 */
- (instancetype)initWithRunningCallback:(nullable void (^)())onEngineRunning
                                 logger:(nullable void (^)(NSInteger, NSString *))logger
                           eventTracker:(nullable void (^)(EnvoyEvent *))eventTracker
                  networkMonitoringMode:(int)networkMonitoringMode;
/**
 Run the Envoy engine with the provided configuration and log level.

 @param config The EnvoyConfiguration used to start Envoy.
 @param logLevel The log level to use when starting Envoy.
 @return A status indicating if the action was successful.
 */
- (int)runWithConfig:(EnvoyConfiguration *)config logLevel:(NSString *)logLevel;

/**
 Opens a new HTTP stream attached to this engine.

 @param callbacks Handler for observing stream events.
 @param explicitFlowControl Whether explicit flow control will be enabled for the stream.
 */
- (id<EnvoyHTTPStream>)startStreamWithCallbacks:(EnvoyHTTPCallbacks *)callbacks
                            explicitFlowControl:(BOOL)explicitFlowControl;

/**
 Increments a counter with the given count.

 @param elements Elements of the counter stat.
 @param count Amount to add to the counter.
 @return A status indicating if the action was successful.
 */
- (int)recordCounterInc:(NSString *)elements tags:(EnvoyTags *)tags count:(NSUInteger)count;

/**
 Retrieve the value of all active stats. Note that this function may block for some time.
 @return The list of active stats and their values, or empty string of the operation failed
 */
- (NSString *)dumpStats;

- (void)terminate;

- (void)resetConnectivityState;

@end

#pragma mark - EnvoyEngineImpl

// Concrete implementation of the `EnvoyEngine` interface.
@interface EnvoyEngineImpl : NSObject <EnvoyEngine>

@end

NS_ASSUME_NONNULL_END
