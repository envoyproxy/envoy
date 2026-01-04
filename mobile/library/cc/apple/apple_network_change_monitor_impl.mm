// NOLINT(namespace-envoy)

#if !TARGET_OS_VISION && !TARGET_OS_WATCH
#import <CoreTelephony/CTTelephonyNetworkInfo.h>
#endif
#import <Foundation/Foundation.h>
#import <Network/Network.h>

#include "library/cc/apple/apple_network_change_monitor_bridge.h"
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

@implementation EnvoyCxxNetworkMonitor {
  std::shared_ptr<Envoy::Platform::NetworkChangeListener> _networkChangeListener;
  nw_path_monitor_t _networkPathMonitor;
  BOOL _wasOffline;
  BOOL _ignoreUpdateOnSameNetwork;
  int _previousNetworkType;
#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
  CTTelephonyNetworkInfo *_telephonyInfo;
#endif
}

- (instancetype)initWithListener:
                    (std::shared_ptr<Envoy::Platform::NetworkChangeListener>)networkChangeListener
            defaultDelegateQueue:(dispatch_queue_t)defaultDelegateQueue
       ignoreUpdateOnSameNetwork:(BOOL)ignoreUpdateOnSameNetwork {
  self = [super init];
  if (self) {
    _networkChangeListener = networkChangeListener;
    _ignoreUpdateOnSameNetwork = ignoreUpdateOnSameNetwork;
    _previousNetworkType = 0;
    _networkPathMonitor = nw_path_monitor_create();
    __weak EnvoyCxxNetworkMonitor *weakSelf = self;
    nw_path_monitor_set_update_handler(_networkPathMonitor, ^(nw_path_t path) {
      [weakSelf checkReachabilityAndNotifyEnvoy:path];
    });
    nw_path_monitor_set_queue(_networkPathMonitor, defaultDelegateQueue);
    // Note that nw_path_monitor_start will call the update handler, which sets the initial
    // network properties.
    nw_path_monitor_start(_networkPathMonitor);
#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
    _telephonyInfo = [[CTTelephonyNetworkInfo alloc] init];
#endif
  }
  return self;
}

- (void)dealloc {
  if (_networkPathMonitor) {
    nw_path_monitor_cancel(_networkPathMonitor);
  }
}

#pragma mark Private Methods

// Helper method to determine the cellular network type using CoreTelephony APIs.
// Returns the network type flags for the cellular network (WWAN plus any sub-type like
// 2G/3G/4G/5G).
- (int)getCellularNetworkType {
  int networkType = static_cast<int>(Envoy::NetworkType::WWAN);
#if !TARGET_OS_VISION && !TARGET_OS_WATCH && !TARGET_OS_OSX
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
  NSString *serviceIdentifier = _telephonyInfo.dataServiceIdentifier;
  if (serviceIdentifier != nil) {
    NSString *technology = _telephonyInfo.serviceCurrentRadioAccessTechnology[serviceIdentifier];
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

- (void)checkReachabilityAndNotifyEnvoy:(nw_path_t)path {
  nw_path_status_t pathStatus = nw_path_get_status(path);
  if (pathStatus == nw_path_status_satisfied || pathStatus == nw_path_status_satisfiable) {
    if (_wasOffline) {
      _wasOffline = NO;
      _networkChangeListener->onDefaultNetworkAvailable();
    }
    int networkType = 0;

    // Check which interface types are available.
    BOOL hasWifiOrWired = nw_path_uses_interface_type(path, nw_interface_type_wifi) ||
                          nw_path_uses_interface_type(path, nw_interface_type_wired);
    BOOL hasCellular = nw_path_uses_interface_type(path, nw_interface_type_cellular);

    if (hasWifiOrWired && hasCellular) {
      // Both WiFi/wired and cellular are available. Use link quality to determine
      // if the WiFi/wired connection is good enough to prefer it over cellular.
#if (defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260000) ||     \
    (defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000)
      if (@available(iOS 26.0, macOS 26.0, *)) {
        nw_path_link_quality_t linkQuality = nw_path_get_link_quality(path);
        // Link quality is for the "best" interface. If quality is poor or unknown, prefer cellular.
        if (linkQuality <= nw_path_link_quality_poor) {
          // Poor WiFi quality, prefer cellular.
          networkType = [self getCellularNetworkType];
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
      networkType = [self getCellularNetworkType];
    } else {
      networkType = static_cast<int>(Envoy::NetworkType::Generic);
    }

    // A network can be both VPN and another type, so we need to check for VPN separately.
    if (nw_path_uses_interface_type(path, nw_interface_type_other)) {
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
    nw_path_monitor_cancel(_networkPathMonitor);
    _networkPathMonitor = nil;
  }
}

@end
