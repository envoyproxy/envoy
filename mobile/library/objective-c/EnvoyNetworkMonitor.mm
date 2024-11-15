#import "library/objective-c/EnvoyEngine.h"

#import "library/common/internal_engine.h"

#import <Foundation/Foundation.h>
#import <Network/Network.h>
#import <SystemConfiguration/SystemConfiguration.h>

@implementation EnvoyNetworkMonitor {
  Envoy::InternalEngine *_engine;
  nw_path_monitor_t _path_monitor;
  SCNetworkReachabilityRef _reachability_ref;
}

- (instancetype)initWithEngine:(envoy_engine_t)engineHandle {
  self = [super init];
  if (!self) {
    return nil;
  }

  _engine = reinterpret_cast<Envoy::InternalEngine *>(engineHandle);
  return self;
}

- (void)dealloc {
  if (_path_monitor) {
    nw_path_monitor_cancel(_path_monitor);
  }
  if (_reachability_ref) {
    SCNetworkReachabilitySetCallback(_reachability_ref, nil, nil);
    SCNetworkReachabilitySetDispatchQueue(_reachability_ref, nil);
    CFRelease(_reachability_ref);
  }
}

- (void)startPathMonitor {
  _path_monitor = nw_path_monitor_create();

  dispatch_queue_attr_t attrs = dispatch_queue_attr_make_with_qos_class(
      DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, DISPATCH_QUEUE_PRIORITY_DEFAULT);
  dispatch_queue_t queue =
      dispatch_queue_create("io.envoyproxy.envoymobile.EnvoyNetworkMonitor", attrs);
  nw_path_monitor_set_queue(_path_monitor, queue);

  __block Envoy::NetworkType previousNetworkType = (Envoy::NetworkType)-1;
  Envoy::InternalEngine *engine = _engine;
  nw_path_monitor_set_update_handler(_path_monitor, ^(nw_path_t _Nonnull path) {
    BOOL isSatisfied = nw_path_get_status(path) == nw_path_status_satisfied;
    if (!isSatisfied) {
      // TODO(jpsim): Handle all possible path status values
      //
      // - nw_path_status_invalid: The path is not valid.
      // - nw_path_status_unsatisfied: The path is not available for use.
      // - nw_path_status_satisfied: The path is available to establish connections and send data.
      // - nw_path_status_satisfiable: The path is not currently available, but establishing a new
      // connection may activate the path.
      return;
    }

    BOOL isCellular = nw_path_uses_interface_type(path, nw_interface_type_cellular);
    Envoy::NetworkType network = Envoy::NetworkType::WWAN;
    if (!isCellular) {
      BOOL isWifi = nw_path_uses_interface_type(path, nw_interface_type_wifi);
      network = isWifi ? Envoy::NetworkType::WLAN : Envoy::NetworkType::Generic;
    }

    if (network != previousNetworkType) {
      NSLog(@"[Envoy] setting preferred network to %d", network);
      engine->onDefaultNetworkChanged(network);
      previousNetworkType = network;
    }

    // TODO(jpsim): Should we shadow or otherwise compare these results with the reachability
    // flags?

    // TODO(jpsim): Should we report back other properties of the reachable path?
    //
    // - nw_path_get_status:
    // https://developer.apple.com/documentation/network/2976886-nw_path_get_status
    // - nw_path_uses_interface_type:
    // https://developer.apple.com/documentation/network/2976898-nw_path_uses_interface_type
    // - nw_path_enumerate_gateways:
    // https://developer.apple.com/documentation/network/3175017-nw_path_enumerate_gateways
    // - nw_path_has_ipv4:
    // https://developer.apple.com/documentation/network/2976888-nw_path_has_ipv4
    // - nw_path_has_ipv6:
    // https://developer.apple.com/documentation/network/2976889-nw_path_has_ipv6
    // - nw_path_has_dns:
    // https://developer.apple.com/documentation/network/2976887-nw_path_has_dns
    // - nw_path_is_constrained:
    // https://developer.apple.com/documentation/network/3131049-nw_path_is_constrained
    // - nw_path_is_expensive:
    // https://developer.apple.com/documentation/network/2976891-nw_path_is_expensive
    // - nw_path_copy_effective_remote_endpoint:
    // https://developer.apple.com/documentation/network/2976883-nw_path_copy_effective_remote_en
  });

  nw_path_monitor_start(_path_monitor);
}

- (void)startReachability {
  NSString *name = @"io.envoyproxy.envoymobile.EnvoyNetworkMonitor";
  SCNetworkReachabilityRef reachability =
      SCNetworkReachabilityCreateWithName(nil, [name UTF8String]);
  if (!reachability) {
    return;
  }

  _reachability_ref = reachability;

  SCNetworkReachabilityContext context = {0, (__bridge void *)self, NULL, NULL, NULL};
  if (!SCNetworkReachabilitySetCallback(_reachability_ref, _reachability_callback, &context)) {
    return;
  }

  dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
  if (!SCNetworkReachabilitySetDispatchQueue(_reachability_ref, queue)) {
    SCNetworkReachabilitySetCallback(_reachability_ref, NULL, NULL);
  }
}

#pragma mark - Private

static void _reachability_callback(SCNetworkReachabilityRef target,
                                   SCNetworkReachabilityFlags flags, void *info) {
  if (flags == 0) {
    return;
  }

#if TARGET_OS_IPHONE
  BOOL isUsingWWAN = flags & kSCNetworkReachabilityFlagsIsWWAN;
#else
  BOOL isUsingWWAN = NO; // Macs don't have WWAN interfaces
#endif

  NSLog(@"[Envoy] setting preferred network to %@", isUsingWWAN ? @"WWAN" : @"WLAN");
  EnvoyNetworkMonitor *monitor = (__bridge EnvoyNetworkMonitor *)info;
  monitor->_engine->onDefaultNetworkChanged(isUsingWWAN ? Envoy::NetworkType::WWAN
                                                        : Envoy::NetworkType::WLAN);
}

@end
