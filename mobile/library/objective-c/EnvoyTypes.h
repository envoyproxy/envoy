#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// MARK: - Aliases

/// Handle to an outstanding Envoy HTTP stream. Valid only for the duration of the stream and not
/// intended for any external interpretation or use.
typedef UInt64 EnvoyStreamID;

/// A set of headers that may be passed to/from an Envoy stream.
typedef NSArray<NSDictionary<NSString *, NSString *> *> EnvoyHeaders;

// MARK: - EnvoyEngineErrorCode

/// Error code associated with terminal status of a HTTP stream.
typedef NS_ENUM(NSUInteger, EnvoyEngineErrorCode) {
  EnvoyEngineErrorCodeStreamReset = 0,
};

// MARK: - EnvoyEngineError

/// Error structure.
@interface EnvoyEngineError : NSError

/// Message with additional details on the error.
@property (nonatomic, copy) NSString *message;

/// Error code representing the Envoy error.
@property (nonatomic, assign) EnvoyEngineErrorCode errorCode;

@end

// MARK: - EnvoyStatus

/// Result codes returned by all calls made to this interface.
typedef NS_CLOSED_ENUM(NSUInteger, EnvoyStatus){
    EnvoyStatusSuccess = 0,
    EnvoyStatusFailure = 1,
};

// MARK: - EnvoyStream

/// Holds data about an HTTP stream.
typedef struct {
  /// Status of the Envoy HTTP stream. Note that the stream might have failed inline.
  /// Thus the status should be checked before pursuing other operations on the stream.
  EnvoyStatus status;

  /// Handle to the Envoy HTTP stream.
  EnvoyStreamID streamID;
} EnvoyStream;

// MARK: - EnvoyObserver

/// Interface that can handle HTTP callbacks.
@interface EnvoyObserver : NSObject

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
@property (nonatomic, strong) void (^onError)(EnvoyEngineError *error);

@end

NS_ASSUME_NONNULL_END
