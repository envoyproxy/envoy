#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyAliases.h"

#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

#pragma mark - EnvoyHTTPStream

@protocol EnvoyHTTPStream

/**
 Open an underlying HTTP stream.

 @param handle Underlying handle of the HTTP stream owned by an Envoy engine.
 @param engine Underlying handle of the Envoy engine.
 @param callbacks The callbacks for the stream.
 @param explicitFlowControl Whether explicit flow control will be enabled for this stream.
 */
- (instancetype)initWithHandle:(intptr_t)handle
                        engine:(intptr_t)engineHandle
                     callbacks:(EnvoyHTTPCallbacks *)callbacks
           explicitFlowControl:(BOOL)explicitFlowControl;

/**
 Send headers over the provided stream.

 @param headers Headers to send over the stream.
 @param close True if the stream should be closed after sending.
 */
- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close;

/**
 Read data from the response stream. Returns immediately.
 Has no effect if explicit flow control is not enabled.

 @param byteCount Maximum number of bytes that may be be passed by the next data callback.
 */
- (void)readData:(size_t)byteCount;

/**
 Send data over the provided stream.

 @param data Data to send over the stream.
 @param close True if the stream should be closed after sending.
 */
- (void)sendData:(NSData *)data close:(BOOL)close;

/**
 Send trailers over the provided stream.

 @param trailers Trailers to send over the stream.
 */
- (void)sendTrailers:(EnvoyHeaders *)trailers;

/**
 Cancel the stream. This functions as an interrupt, and aborts further callbacks and handling of the
 stream.

 @return Success unless the stream has already been canceled.
 */
- (int)cancel;

/**
 Clean up the stream after it's closed (by completion, cancellation, or error).
 */
- (void)cleanUp;

@end

#pragma mark - EnvoyHTTPStreamImpl

// Concrete implementation of the `EnvoyHTTPStream` protocol.
@interface EnvoyHTTPStreamImpl : NSObject <EnvoyHTTPStream>

@end

NS_ASSUME_NONNULL_END
