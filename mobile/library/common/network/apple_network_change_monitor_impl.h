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

@protocol EnvoyNetworkMonitorProvider <NSObject>
- (nw_path_monitor_t)createMonitor;
- (void)setUpdateHandler:(nw_path_monitor_t)monitor handler:(void (^)(nw_path_t))handler;
- (void)setQueue:(nw_path_monitor_t)monitor queue:(dispatch_queue_t)queue;
- (void)start:(nw_path_monitor_t)monitor;
- (void)cancel:(nw_path_monitor_t)monitor;
- (nw_path_status_t)extractStatus:(nw_path_t)path;
- (BOOL)usesInterfaceType:(nw_path_t)path type:(nw_interface_type_t)type;
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
- (nw_path_link_quality_t)extractLinkQuality:(nw_path_t)path;
#endif
- (int)getCellularNetworkType;
@end

@interface EnvoyDefaultNetworkMonitorProvider : NSObject <EnvoyNetworkMonitorProvider>
@end

/**
 * Objective-C implementation of network change monitoring using Apple's Network framework.
 */
@interface EnvoyCxxNetworkMonitor : NSObject

- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork;

- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork
                        provider:(id<EnvoyNetworkMonitorProvider>)provider;

- (void)stop;
@end
