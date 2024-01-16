#pragma once

#import <Foundation/Foundation.h>

#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

// Monitors network changes in order to update Envoy network cluster preferences.
@interface EnvoyNetworkMonitor : NSObject

/**
 Create a new instance of the network monitor.
 */
- (instancetype)initWithEngine:(envoy_engine_t)engineHandle;

// Start monitoring reachability using `SCNetworkReachability`, updating the
// preferred Envoy network cluster on changes.
// This is typically called by `EnvoyEngine` automatically on startup.
- (void)startReachability;

// Start monitoring reachability using `NWPathMonitor`, updating the
// preferred Envoy network cluster on changes.
// This is typically called by `EnvoyEngine` automatically on startup.
- (void)startPathMonitor;

@end

NS_ASSUME_NONNULL_END
