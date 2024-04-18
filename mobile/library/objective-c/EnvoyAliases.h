#pragma once

#import <Foundation/Foundation.h>

#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

/// Return codes for Engine interface. @see /library/common/types/c_types.h
extern const int kEnvoySuccess;
extern const int kEnvoyFailure;

/// A set of headers that may be passed to/from an Envoy stream.
typedef NSDictionary<NSString *, NSArray<NSString *> *> EnvoyHeaders;

typedef NSDictionary<NSString *, NSString *> EnvoyTags;

/// A set of key-value pairs describing an event.
typedef NSDictionary<NSString *, NSString *> EnvoyEvent;

/// Contains internal HTTP stream metrics, context, and other details.
typedef envoy_stream_intel EnvoyStreamIntel;

// Contains one time HTTP stream metrics, context, and other details.
typedef envoy_final_stream_intel EnvoyFinalStreamIntel;

NS_ASSUME_NONNULL_END
