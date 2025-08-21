#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyAliases.h"

#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

/// Return codes for on-headers filter invocations. @see envoy/http/filter.h
extern const int kEnvoyFilterHeadersStatusContinue;
extern const int kEnvoyFilterHeadersStatusStopIteration;
extern const int kEnvoyFilterHeadersStatusStopAllIterationAndBuffer;

/// Return codes for on-data filter invocations. @see envoy/http/filter.h
extern const int kEnvoyFilterDataStatusContinue;
extern const int kEnvoyFilterDataStatusStopIterationAndBuffer;
extern const int kEnvoyFilterDataStatusStopIterationNoBuffer;
extern const int kEnvoyFilterDataStatusResumeIteration;

/// Return codes for on-trailers filter invocations. @see envoy/http/filter.h
extern const int kEnvoyFilterTrailersStatusContinue;
extern const int kEnvoyFilterTrailersStatusStopIteration;
extern const int kEnvoyFilterTrailersStatusResumeIteration;

/// Return codes for on-resume filter invocations. These are unique to platform filters,
/// and used exclusively after an asynchronous request to resume iteration via callbacks.
extern const int kEnvoyFilterResumeStatusStopIteration;
extern const int kEnvoyFilterResumeStatusResumeIteration;

/// Callbacks for asynchronous interaction with the filter.
@protocol EnvoyHTTPFilterCallbacks

/// Resume filter iteration asynchronously. This will result in an on-resume invocation of the
/// filter.
- (void)resumeIteration;

/// Reset the underlying stream idle timeout to its configured threshold. This may be useful if
/// a filter stops iteration for an extended period of time, since ordinarily timeouts will still
/// apply. This may be called periodically to continue to indicate "activity" on the stream.
- (void)resetIdleTimer;

@end

@interface EnvoyHTTPFilter : NSObject

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, forward headers
@property (nonatomic, copy, nullable) NSArray * (^onRequestHeaders)
    (EnvoyHeaders *headers, BOOL endStream, EnvoyStreamIntel streamIntel);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - NSData *, forward data
/// 2 - EnvoyHeaders *, optional pending headers
@property (nonatomic, copy, nullable) NSArray * (^onRequestData)
    (NSData *data, BOOL endStream, EnvoyStreamIntel streamIntel);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, forward trailers
/// 2 - EnvoyHeaders *, optional pending headers
/// 3 - NSData *, optional pending data
@property (nonatomic, copy, nullable) NSArray * (^onRequestTrailers)
    (EnvoyHeaders *trailers, EnvoyStreamIntel streamIntel);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, forward headers
@property (nonatomic, copy, nullable) NSArray * (^onResponseHeaders)
    (EnvoyHeaders *headers, BOOL endStream, EnvoyStreamIntel streamIntel);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - NSData *, forward data
/// 2 - EnvoyHeaders *, optional pending headers
@property (nonatomic, copy, nullable) NSArray * (^onResponseData)
    (NSData *data, BOOL endStream, EnvoyStreamIntel streamIntel);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, forward trailers
/// 2 - EnvoyHeaders *, optional pending headers
/// 3 - NSData *, optional pending data
@property (nonatomic, copy, nullable) NSArray * (^onResponseTrailers)
    (EnvoyHeaders *trailers, EnvoyStreamIntel streamIntel);

@property (nonatomic, copy, nullable) void (^onCancel)
    (EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel);

@property (nonatomic, copy, nullable) void (^onError)
    (uint64_t errorCode, NSString *message, int32_t attemptCount, EnvoyStreamIntel streamIntel,
     EnvoyFinalStreamIntel finalStreamIntel);

@property (nonatomic, copy, nullable) void (^onComplete)
    (EnvoyStreamIntel streamIntel, EnvoyFinalStreamIntel finalStreamIntel);

@property (nonatomic, copy, nullable) void (^setRequestFilterCallbacks)
    (id<EnvoyHTTPFilterCallbacks> callbacks);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, optional pending headers
/// 2 - NSData *, optional pending data
/// 3 - EnvoyHeaders *, optional pending trailers
@property (nonatomic, copy, nullable) NSArray * (^onResumeRequest)
    (EnvoyHeaders *_Nullable headers, NSData *_Nullable data, EnvoyHeaders *_Nullable trailers,
     BOOL endStream, EnvoyStreamIntel streamIntel);

@property (nonatomic, copy, nullable) void (^setResponseFilterCallbacks)
    (id<EnvoyHTTPFilterCallbacks> callbacks);

/// Returns tuple of:
/// 0 - NSNumber *,filter status
/// 1 - EnvoyHeaders *, optional pending headers
/// 2 - NSData *, optional pending data
/// 3 - EnvoyHeaders *, optional pending trailers
@property (nonatomic, copy, nullable) NSArray * (^onResumeResponse)
    (EnvoyHeaders *_Nullable headers, NSData *_Nullable data, EnvoyHeaders *_Nullable trailers,
     BOOL endStream, EnvoyStreamIntel streamIntel);

@end

NS_ASSUME_NONNULL_END
