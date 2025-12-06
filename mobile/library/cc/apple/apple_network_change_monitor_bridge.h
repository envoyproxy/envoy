#pragma once

#if !TARGET_OS_VISION && !TARGET_OS_WATCH
#import <CoreTelephony/CTTelephonyNetworkInfo.h>
#endif
#import <Foundation/Foundation.h>
#import <Network/Network.h>

#include "library/cc/engine.h"
#include "library/common/engine_types.h"
#include "library/common/network/network_types.h"

/**
 * Objective-C implementation of network change monitoring using Apple's Network framework.
 */
@interface HAMEnvoyNetworkMonitor : NSObject
- (instancetype)initWithEngine:(std::shared_ptr<Envoy::Platform::Engine>)internalEngine
          defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
     ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork;

- (void)stop;
@end
