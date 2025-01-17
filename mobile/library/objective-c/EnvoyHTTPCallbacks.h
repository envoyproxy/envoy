#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyAliases.h"

NS_ASSUME_NONNULL_BEGIN

#pragma mark - EnvoyHTTPCallbacks

/// Interface that can handle callbacks from an HTTP stream.
@interface EnvoyHTTPCallbacks : NSObject

/**
 * Dispatch queue provided to handle callbacks.
 */
@property (nonatomic, assign) dispatch_queue_t dispatchQueue;

/**
 * Called when all headers get received on the async HTTP stream.
 * @param headers the headers received.
 * @param endStream whether the response is headers-only.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onHeaders)
    (EnvoyHeaders *headers, BOOL endStream, EnvoyStreamIntel streamIntel);

/**
 * Called when a data frame gets received on the async HTTP stream.
 * This callback can be invoked multiple times if the data gets streamed.
 * @param data the data received.
 * @param endStream whether the data is the last data frame.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onData)
    (NSData *data, BOOL endStream, EnvoyStreamIntel streamIntel);

/**
 * Called when all trailers get received on the async HTTP stream.
 * Note that end stream is implied when on_trailers is called.
 * @param trailers the trailers received.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onTrailers)
    (EnvoyHeaders *trailers, EnvoyStreamIntel streamIntel);

/**
 * Called to signal there is buffer space available for continued request body upload.
 *
 * This is only ever called when the library is in explicit flow control mode. When enabled,
 * the issuer should wait for this callback after calling sendData, before making another call
 * to sendData.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onSendWindowAvailable)(EnvoyStreamIntel streamIntel);

/**
 * Called when the async HTTP stream has an error.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 * @param finalStreamIntel one time HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onError)
    (uint64_t errorCode, NSString *message, int32_t attemptCount, EnvoyStreamIntel streamIntel,
     EnvoyFinalStreamIntel finalStreamIntel);

/**
 * Called when the async HTTP stream is canceled.
 * Note this callback will ALWAYS be fired if a stream is canceled, even if the request and/or
 * response is already complete. It will fire no more than once, and no other callbacks for the
 * stream will be issued afterwards.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 * @param finalStreamIntel one time HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onCancel)
    (EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel);

/**
 * Final call made when an HTTP stream is closed gracefully.
 * Note this may already be inferred from a prior callback with endStream=TRUE, and this only needs
 * to be handled if information from finalStreamIntel is desired.
 * @param streamIntel internal HTTP stream metrics, context, and other details.
 * @param finalStreamIntel one time HTTP stream metrics, context, and other details.
 */
@property (nonatomic, copy, nullable) void (^onComplete)
    (EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel);

@end

NS_ASSUME_NONNULL_END
