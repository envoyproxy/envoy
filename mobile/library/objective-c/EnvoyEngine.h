#import <Foundation/Foundation.h>

/// Wrapper layer to simplify calling into Envoy's C++ API.
@interface EnvoyEngine : NSObject

/**
 Run the Envoy engine with the provided config and log level.

 @param config The configuration file with which to start Envoy.
 @return A status indicating if the action was successful.
 */
+ (int)runWithConfig:(NSString *)config;

/**
 Run the Envoy engine with the provided config and log level.

 @param config The configuration file with which to start Envoy.
 @param logLevel The log level to use when starting Envoy.
 @return A status indicating if the action was successful.
 */
+ (int)runWithConfig:(NSString *)config logLevel:(NSString *)logLevel;

@end
