#import "EnvoyTypes.h"

#import <Foundation/Foundation.h>

/// Wrapper layer to simplify calling into Envoy's C/++ API.
@interface EnvoyEngine : NSObject

/**
 Run the Envoy engine with the provided config and log level.

 @param config The configuration file with which to start Envoy.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)runWithConfig:(NSString *)config;

/**
 Run the Envoy engine with the provided config and log level.

 @param config The configuration file with which to start Envoy.
 @param logLevel The log level to use when starting Envoy.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)runWithConfig:(NSString *)config logLevel:(NSString *)logLevel;

/**
 Open an underlying HTTP stream.

 @param observer the observer that will run the stream callbacks.
 @return stream with a handle and success status, or a failure status.
 */
+ (EnvoyStream)startStreamWithObserver:(EnvoyObserver *)observer;

/**
 Send headers over the provided stream.

 @param metadata Headers to send over the stream.
 @param stream The stream over which to send headers.
 @param close True if the stream should be closed after sending.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)sendHeaders:(EnvoyHeaders *)headers to:(EnvoyStream *)stream close:(BOOL)close;

/**
 Send data over the provided stream.

 @param metadata Data to send over the stream.
 @param stream The stream over which to send data.
 @param close True if the stream should be closed after sending.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)sendData:(NSData *)data to:(EnvoyStream *)stream close:(BOOL)close;

/**
 Send metadata over the provided stream.

 @param metadata Metadata to send over the stream.
 @param stream The stream over which to send metadata.
 @param close True if the stream should be closed after sending.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)sendMetadata:(EnvoyHeaders *)metadata to:(EnvoyStream *)stream close:(BOOL)close;

/**
 Send trailers over the provided stream.

 @param trailers Trailers to send over the stream.
 @param stream The stream over which to send trailers.
 @param close True if the stream should be closed after sending.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)sendTrailers:(EnvoyHeaders *)trailers to:(EnvoyStream *)stream close:(BOOL)close;

/**
 Cancel and end the stream.

 @param stream The stream to close.
 @return The stream to close.
 */
+ (EnvoyStatus)locallyCloseStream:(EnvoyStream *)stream;

/**
 Reset the stream.

 @param stream The stream to reset.
 @return A status indicating if the action was successful.
 */
+ (EnvoyStatus)resetStream:(EnvoyStream *)stream;

@end
