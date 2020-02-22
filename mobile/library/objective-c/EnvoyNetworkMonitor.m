#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

#import <SystemConfiguration/SystemConfiguration.h>

@implementation EnvoyNetworkMonitor

+ (void)startReachabilityIfNeeded {
  static dispatch_once_t reachabilityStarted;
  dispatch_once(&reachabilityStarted, ^{
    _start_reachability();
  });
}

#pragma mark - Private

static SCNetworkReachabilityRef _reachability_ref;

static void _reachability_callback(SCNetworkReachabilityRef target,
                                   SCNetworkReachabilityFlags flags, void *info) {
  if (flags == 0) {
    return;
  }

  BOOL isUsingWWAN = flags & kSCNetworkReachabilityFlagsIsWWAN;
  NSLog(@"[Envoy] setting preferred network to %@", isUsingWWAN ? @"WWAN" : @"WLAN");
  set_preferred_network(isUsingWWAN ? ENVOY_NET_WWAN : ENVOY_NET_WLAN);
}

static void _start_reachability() {
  NSString *name = @"io.envoyproxy.envoymobile.EnvoyNetworkMonitor";
  SCNetworkReachabilityRef reachability =
      SCNetworkReachabilityCreateWithName(nil, [name UTF8String]);
  if (!reachability) {
    return;
  }

  _reachability_ref = reachability;

  SCNetworkReachabilityContext context = {0, NULL, NULL, NULL, NULL};
  if (!SCNetworkReachabilitySetCallback(_reachability_ref, _reachability_callback, &context)) {
    return;
  }

  dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0);
  if (!SCNetworkReachabilitySetDispatchQueue(_reachability_ref, queue)) {
    SCNetworkReachabilitySetCallback(_reachability_ref, NULL, NULL);
  }
}

@end
