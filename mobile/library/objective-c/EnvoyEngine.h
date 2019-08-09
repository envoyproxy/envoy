#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// MARK: - Aliases

/// A set of headers that may be passed to/from an Envoy stream.
typedef NSDictionary<NSString *, NSArray<NSString *> *> EnvoyHeaders;

// MARK: - EnvoyObserver

/// Interface that can handle HTTP callbacks.
// FIXME: can we just bridge the swift object here and/or just expose this to swift as the
// ResponseHandler?
@interface EnvoyObserver : NSObject

/**
 * Dispatch queue provided to handle callbacks.
 */
@property (nonatomic, assign) dispatch_queue_t dispatchQueue;

/**
 * Called when all headers get received on the async HTTP stream.
 * @param headers the headers received.
 * @param endStream whether the response is headers-only.
 */
@property (nonatomic, strong) void (^onHeaders)(EnvoyHeaders *headers, BOOL endStream);

/**
 * Called when a data frame gets received on the async HTTP stream.
 * This callback can be invoked multiple times if the data gets streamed.
 * @param data the data received.
 * @param endStream whether the data is the last data frame.
 */
@property (nonatomic, strong) void (^onData)(NSData *data, BOOL endStream);

/**
 * Called when all metadata gets received on the async HTTP stream.
 * Note that end stream is implied when on_trailers is called.
 * @param metadata the metadata received.
 */
@property (nonatomic, strong) void (^onMetadata)(EnvoyHeaders *metadata);

/**
 * Called when all trailers get received on the async HTTP stream.
 * Note that end stream is implied when on_trailers is called.
 * @param trailers the trailers received.
 */
@property (nonatomic, strong) void (^onTrailers)(EnvoyHeaders *trailers);

/**
 * Called when the async HTTP stream has an error.
 * @param error the error received/caused by the async HTTP stream.
 */
@property (nonatomic, strong) void (^onError)();

// FIXME
@property (nonatomic, strong) void (^onCancel)();

@end

/// Wrapper layer for calling into Envoy's C/++ API.
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

/// Performs necessary setup after Envoy has initialized and started running.
/// TODO: create a post-initialization callback from Envoy to handle this automatically.
+ (void)setupEnvoy;

@end

@interface EnvoyHttpStream : NSObject
/**
 Open an underlying HTTP stream.

 @param observer the observer that will run the stream callbacks.
 */
- (instancetype)initWithObserver:(EnvoyObserver *)observer;

/**
 Send headers over the provided stream.

 @param headers Headers to send over the stream.
 @param close True if the stream should be closed after sending.
 */
- (void)sendHeaders:(EnvoyHeaders *)headers close:(BOOL)close;

/**
 Send data over the provided stream.

 @param data Data to send over the stream.
 @param close True if the stream should be closed after sending.
 */
- (void)sendData:(NSData *)data close:(BOOL)close;

/**
 Send metadata over the provided stream.

 @param metadata Metadata to send over the stream.
 */
- (void)sendMetadata:(EnvoyHeaders *)metadata;

/**
 Send trailers over the provided stream.

 @param trailers Trailers to send over the stream.
 */
- (void)sendTrailers:(EnvoyHeaders *)trailers;

/**
 Cancel the stream. This functions as an interrupt, and aborts further callbacks and handling of the
 stream.
 @return Success, unless the stream has already been canceled.
 */
- (int)cancel;

@end

NS_ASSUME_NONNULL_END
