// NOLINT(namespace-envoy)

#if !TARGET_OS_VISION && !TARGET_OS_WATCH
#import <CoreTelephony/CTTelephonyNetworkInfo.h>
#endif
#import <Foundation/Foundation.h>
#import <Network/Network.h>

#include "library/common/network/apple_network_change_monitor_impl.h"
#include "library/common/engine_types.h"
#include "library/common/network/network_types.h"

#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
static NSString *RadioAccessTechnologyNRNSA() {
  // iOS 14.2.0 beta has not defined @c CTRadioAccessTechnologyNRNSA.
  if (@available(iOS 14.2.1, *)) {
    return CTRadioAccessTechnologyNRNSA;
  } else {
    return @"CTRadioAccessTechnologyNRNSA";
  }
}
#endif

#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
static NSString *RadioAccessTechnologyNR() {
  // OS 14.2.0 beta has not defined @c CTRadioAccessTechnologyNR.
  if (@available(iOS 14.2.1, *)) {
    return CTRadioAccessTechnologyNR;
  } else {
    return @"CTRadioAccessTechnologyNR";
  }
}
#endif

@implementation EnvoyDefaultNetworkMonitorProvider

- (nw_path_monitor_t)createMonitor {
  return nw_path_monitor_create();
}

- (void)setUpdateHandler:(nw_path_monitor_t)monitor handler:(void (^)(nw_path_t))handler {
  nw_path_monitor_set_update_handler(monitor, handler);
}

- (void)setQueue:(nw_path_monitor_t)monitor queue:(dispatch_queue_t)queue {
  nw_path_monitor_set_queue(monitor, queue);
}

- (void)start:(nw_path_monitor_t)monitor {
  nw_path_monitor_start(monitor);
}

- (void)cancel:(nw_path_monitor_t)monitor {
  nw_path_monitor_cancel(monitor);
}

- (nw_path_status_t)extractStatus:(nw_path_t)path {
  return nw_path_get_status(path);
}

- (BOOL)usesInterfaceType:(nw_path_t)path type:(nw_interface_type_t)type {
  return nw_path_uses_interface_type(path, type);
}

#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
- (nw_path_link_quality_t)extractLinkQuality:(nw_path_t)path {
  return nw_path_get_link_quality(path);
}
#endif

// Helper method to determine the cellular network type using CoreTelephony APIs.
// Returns the network type flags for the cellular network (WWAN plus any sub-type like
// 2G/3G/4G/5G).
- (int)getCellularNetworkType {
  int networkType = static_cast<int>(Envoy::NetworkType::WWAN);
#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
  CTTelephonyNetworkInfo *telephonyInfo = [[CTTelephonyNetworkInfo alloc] init];
  // Check the sub-type of the cellular network.
  NSSet<NSString *> *technologies2g =
      [NSSet setWithObjects:CTRadioAccessTechnologyGPRS, CTRadioAccessTechnologyEdge,
                            CTRadioAccessTechnologyCDMA1x, nil];
  NSSet<NSString *> *technologies3g =
      [NSSet setWithObjects:CTRadioAccessTechnologyWCDMA, CTRadioAccessTechnologyHSDPA,
                            CTRadioAccessTechnologyHSUPA, CTRadioAccessTechnologyCDMAEVDORev0,
                            CTRadioAccessTechnologyCDMAEVDORevA,
                            CTRadioAccessTechnologyCDMAEVDORevB, CTRadioAccessTechnologyeHRPD, nil];
  NSSet<NSString *> *technologies4g = [NSSet setWithObjects:CTRadioAccessTechnologyLTE, nil];
  NSSet<NSString *> *technologies5g =
      [NSSet setWithObjects:RadioAccessTechnologyNR(), RadioAccessTechnologyNRNSA(), nil];
  NSString *serviceIdentifier = telephonyInfo.dataServiceIdentifier;
  if (serviceIdentifier != nil) {
    NSString *technology = telephonyInfo.serviceCurrentRadioAccessTechnology[serviceIdentifier];
    if (technology != nil) {
      if ([technologies2g containsObject:technology]) {
        networkType |= static_cast<int>(Envoy::NetworkType::WWAN_2G);
      } else if ([technologies3g containsObject:technology]) {
        networkType |= static_cast<int>(Envoy::NetworkType::WWAN_3G);
      } else if ([technologies4g containsObject:technology]) {
        networkType |= static_cast<int>(Envoy::NetworkType::WWAN_4G);
      } else if ([technologies5g containsObject:technology]) {
        networkType |= static_cast<int>(Envoy::NetworkType::WWAN_5G);
      }
    }
  }
#endif
  return networkType;
}

@end

@implementation EnvoyCxxNetworkMonitor {
  std::shared_ptr<Envoy::Platform::NetworkChangeListener> _networkChangeListener;
  nw_path_monitor_t _networkPathMonitor;
  id<EnvoyNetworkMonitorProvider> _provider;
  BOOL _wasOffline;
  BOOL _ignoreUpdateOnSameNetwork;
  int _previousNetworkType;
}

- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork {
  return [self initWithListener:networkChangeListener
           defaultDelegateQueue:defaultDelegateQueue
      ignoreUpdateOnSameNetwork:ignoreUpdateOnSameNetwork
                       provider:[[EnvoyDefaultNetworkMonitorProvider alloc] init]];
}

- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork
                        provider:(id<EnvoyNetworkMonitorProvider>)provider {
  self = [super init];
  if (self) {
    _provider = provider;
    _networkChangeListener = networkChangeListener;
    _ignoreUpdateOnSameNetwork = ignoreUpdateOnSameNetwork;
    _previousNetworkType = 0;
    _networkPathMonitor = [_provider createMonitor];
    __weak EnvoyCxxNetworkMonitor *weakSelf = self;
    [_provider setUpdateHandler:_networkPathMonitor
                        handler:^(nw_path_t path) {
                          [weakSelf checkReachabilityAndNotifyEnvoy:path];
                        }];
    [_provider setQueue:_networkPathMonitor queue:defaultDelegateQueue];
    // Note that nw_path_monitor_start will call the update handler, which sets the initial
    // network properties.
    [_provider start:_networkPathMonitor];
  }
  return self;
}

- (void)dealloc {
  if (_networkPathMonitor) {
    [_provider cancel:_networkPathMonitor];
  }
}

#pragma mark Private Methods

- (void)checkReachabilityAndNotifyEnvoy:(nw_path_t)path {
  nw_path_status_t pathStatus = [_provider extractStatus:path];
  if (pathStatus == nw_path_status_satisfied || pathStatus == nw_path_status_satisfiable) {
    if (_wasOffline) {
      _wasOffline = NO;
      _networkChangeListener->onDefaultNetworkAvailable();
    }
    int networkType = 0;

    // Check which interface types are available.
    BOOL hasWifiOrWired = [_provider usesInterfaceType:path type:nw_interface_type_wifi] ||
                          [_provider usesInterfaceType:path type:nw_interface_type_wired];
    BOOL hasCellular = [_provider usesInterfaceType:path type:nw_interface_type_cellular];

    if (hasWifiOrWired && hasCellular) {
      // Both WiFi/wired and cellular are available. Use link quality to determine
      // if the WiFi/wired connection is good enough to prefer it over cellular.
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
      if (@available(iOS 26.0, macOS 26.0, *)) {
        nw_path_link_quality_t linkQuality = [_provider extractLinkQuality:path];
        // Link quality is for the "best" interface. If quality is poor or unknown, prefer cellular.
        if (linkQuality <= nw_path_link_quality_poor) {
          // Poor WiFi quality, prefer cellular.
          networkType = [_provider getCellularNetworkType];
        } else {
          // WiFi/wired quality is acceptable, prefer it.
          networkType = static_cast<int>(Envoy::NetworkType::WLAN);
        }
      } else {
        // Fallback for older OS versions: prefer WiFi/wired over cellular.
        networkType = static_cast<int>(Envoy::NetworkType::WLAN);
      }
#else
      // SDK doesn't have link quality APIs, prefer WiFi/wired over cellular.
      networkType = static_cast<int>(Envoy::NetworkType::WLAN);
#endif
    } else if (hasWifiOrWired) {
      networkType = static_cast<int>(Envoy::NetworkType::WLAN);
    } else if (hasCellular) {
      networkType = [_provider getCellularNetworkType];
    } else {
      networkType = static_cast<int>(Envoy::NetworkType::Generic);
    }

    // A network can be both VPN and another type, so we need to check for VPN separately.
    if ([_provider usesInterfaceType:path type:nw_interface_type_other]) {
      networkType |= static_cast<int>(Envoy::NetworkType::Generic);
    }
    _networkChangeListener->onDefaultNetworkChangeEvent(networkType);
    _previousNetworkType = networkType;
  } else {
    if (!_wasOffline) {
      _wasOffline = YES;
      _networkChangeListener->onDefaultNetworkUnavailable();
    }
  }
}

- (void)stop {
  if (_networkPathMonitor) {
    [_provider cancel:_networkPathMonitor];
    _networkPathMonitor = nil;
  }
}

@end
