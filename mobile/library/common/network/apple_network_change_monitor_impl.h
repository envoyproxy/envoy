#pragma once

#if !TARGET_OS_VISION && !TARGET_OS_WATCH
#import <CoreTelephony/CTTelephonyNetworkInfo.h>
#endif
#import <Foundation/Foundation.h>
#import <Network/Network.h>

#include <memory>

#include "library/cc/network_change_monitor.h"

namespace Envoy {
namespace Platform {
class NetworkChangeListener;
} // namespace Platform
} // namespace Envoy

/**
 * Objective-C implementation of network change monitoring using Apple's Network framework.
 */
@interface EnvoyCxxNetworkMonitor : NSObject
- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork;

- (void)stop;
@end
