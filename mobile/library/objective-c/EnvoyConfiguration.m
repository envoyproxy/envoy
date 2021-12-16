#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

- (instancetype)initWithAdminInterfaceEnabled:(BOOL)adminInterfaceEnabled
                                  GrpcStatsDomain:(nullable NSString *)grpcStatsDomain
                            connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                                dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                     dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
                      dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                           dnsQueryTimeoutSeconds:(UInt32)dnsQueryTimeoutSeconds
                           dnsPreresolveHostnames:(NSString *)dnsPreresolveHostnames
                           enableInterfaceBinding:(BOOL)enableInterfaceBinding
    h2ConnectionKeepaliveIdleIntervalMilliseconds:
        (UInt32)h2ConnectionKeepaliveIdleIntervalMilliseconds
              h2ConnectionKeepaliveTimeoutSeconds:(UInt32)h2ConnectionKeepaliveTimeoutSeconds
                                statsFlushSeconds:(UInt32)statsFlushSeconds
                         streamIdleTimeoutSeconds:(UInt32)streamIdleTimeoutSeconds
                         perTryIdleTimeoutSeconds:(UInt32)perTryIdleTimeoutSeconds
                                       appVersion:(NSString *)appVersion
                                            appId:(NSString *)appId
                                  virtualClusters:(NSString *)virtualClusters
                           directResponseMatchers:(NSString *)directResponseMatchers
                                  directResponses:(NSString *)directResponses
                                nativeFilterChain:
                                    (NSArray<EnvoyNativeFilterConfig *> *)nativeFilterChain
                              platformFilterChain:
                                  (NSArray<EnvoyHTTPFilterFactory *> *)httpPlatformFilterFactories
                                  stringAccessors:
                                      (NSDictionary<NSString *, EnvoyStringAccessor *> *)
                                          stringAccessors {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.adminInterfaceEnabled = adminInterfaceEnabled;
  self.grpcStatsDomain = grpcStatsDomain;
  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
  self.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
  self.dnsQueryTimeoutSeconds = dnsQueryTimeoutSeconds;
  self.dnsPreresolveHostnames = dnsPreresolveHostnames;
  self.enableInterfaceBinding = enableInterfaceBinding;
  self.h2ConnectionKeepaliveIdleIntervalMilliseconds =
      h2ConnectionKeepaliveIdleIntervalMilliseconds;
  self.h2ConnectionKeepaliveTimeoutSeconds = h2ConnectionKeepaliveTimeoutSeconds;
  self.statsFlushSeconds = statsFlushSeconds;
  self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
  self.perTryIdleTimeoutSeconds = perTryIdleTimeoutSeconds;
  self.appVersion = appVersion;
  self.appId = appId;
  self.virtualClusters = virtualClusters;
  self.directResponseMatchers = directResponseMatchers;
  self.directResponses = directResponses;
  self.nativeFilterChain = nativeFilterChain;
  self.httpPlatformFilterFactories = httpPlatformFilterFactories;
  self.stringAccessors = stringAccessors;
  return self;
}

- (nullable NSString *)resolveTemplate:(NSString *)templateYAML {
  NSMutableString *customClusters = [[NSMutableString alloc] init];
  NSMutableString *customListeners = [[NSMutableString alloc] init];
  NSMutableString *customRoutes = [[NSMutableString alloc] init];
  NSMutableString *customFilters = [[NSMutableString alloc] init];

  NSString *platformFilterTemplate = [[NSString alloc] initWithUTF8String:platform_filter_template];
  for (EnvoyHTTPFilterFactory *filterFactory in self.httpPlatformFilterFactories) {
    NSString *filterConfig =
        [platformFilterTemplate stringByReplacingOccurrencesOfString:@"{{ platform_filter_name }}"
                                                          withString:filterFactory.filterName];
    [customFilters appendString:filterConfig];
  }

  NSString *nativeFilterTemplate = [[NSString alloc] initWithUTF8String:native_filter_template];
  for (EnvoyNativeFilterConfig *nativeFilterConfig in self.nativeFilterChain) {
    NSString *filterConfig =
        [[nativeFilterTemplate stringByReplacingOccurrencesOfString:@"{{ native_filter_name }}"
                                                         withString:nativeFilterConfig.name]
            stringByReplacingOccurrencesOfString:@"{{ native_filter_typed_config }}"
                                      withString:nativeFilterConfig.typedConfig];
    [customFilters appendString:filterConfig];
  }

  BOOL hasDirectResponses = self.directResponses.length > 0;
  if (hasDirectResponses) {
    templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"#{fake_remote_responses}"
                                                           withString:self.directResponses];
    [customClusters appendString:[[NSString alloc] initWithUTF8String:fake_remote_cluster_insert]];
    [customListeners
        appendString:[[NSString alloc] initWithUTF8String:fake_remote_listener_insert]];
    [customRoutes appendString:self.directResponseMatchers];
    [customFilters
        appendString:[[NSString alloc] initWithUTF8String:route_cache_reset_filter_insert]];
  }

  templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"#{custom_clusters}"
                                                         withString:customClusters];
  templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"#{custom_listeners}"
                                                         withString:customListeners];
  templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"#{custom_routes}"
                                                         withString:customRoutes];
  templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"#{custom_filters}"
                                                         withString:customFilters];

  NSMutableString *definitions =
      [[NSMutableString alloc] initWithString:@"!ignore platform_defs:\n"];

  [definitions
      appendFormat:@"- &connect_timeout %lus\n", (unsigned long)self.connectTimeoutSeconds];
  [definitions appendFormat:@"- &dns_refresh_rate %lus\n", (unsigned long)self.dnsRefreshSeconds];
  [definitions appendFormat:@"- &dns_fail_base_interval %lus\n",
                            (unsigned long)self.dnsFailureRefreshSecondsBase];
  [definitions appendFormat:@"- &dns_fail_max_interval %lus\n",
                            (unsigned long)self.dnsFailureRefreshSecondsMax];
  [definitions
      appendFormat:@"- &dns_query_timeout %lus\n", (unsigned long)self.dnsQueryTimeoutSeconds];
  [definitions appendFormat:@"- &dns_preresolve_hostnames %@\n", self.dnsPreresolveHostnames];
  [definitions appendFormat:@"- &dns_resolver_name envoy.network.dns_resolver.apple\n"];
  // No additional values are currently needed for Apple-based DNS resolver.
  [definitions appendFormat:@"- &dns_resolver_config {}\n"];
  [definitions appendFormat:@"- &enable_interface_binding %@\n",
                            self.enableInterfaceBinding ? @"true" : @"false"];
  [definitions appendFormat:@"- &h2_connection_keepalive_idle_interval %.*fs\n", 3,
                            (double)self.h2ConnectionKeepaliveIdleIntervalMilliseconds / 1000.0];
  [definitions appendFormat:@"- &h2_connection_keepalive_timeout %lus\n",
                            (unsigned long)self.h2ConnectionKeepaliveTimeoutSeconds];
  [definitions
      appendFormat:@"- &stream_idle_timeout %lus\n", (unsigned long)self.streamIdleTimeoutSeconds];
  [definitions
      appendFormat:@"- &per_try_idle_timeout %lus\n", (unsigned long)self.perTryIdleTimeoutSeconds];
  [definitions appendFormat:@"- &metadata { device_os: %@, app_version: %@, app_id: %@ }\n", @"iOS",
                            self.appVersion, self.appId];
  [definitions appendFormat:@"- &virtual_clusters %@\n", self.virtualClusters];

  if (self.grpcStatsDomain != nil) {
    [definitions appendFormat:@"- &stats_domain %@\n", self.grpcStatsDomain];
    [definitions
        appendFormat:@"- &stats_flush_interval %lus\n", (unsigned long)self.statsFlushSeconds];
    [definitions appendString:@"- &stats_sinks [ *base_metrics_service ]\n"];
  }

  if (self.adminInterfaceEnabled) {
    [definitions appendString:@"admin: *admin_interface\n"];
  }

  [definitions appendString:templateYAML];

  if ([definitions containsString:@"{{"]) {
    NSLog(@"[Envoy] error: could not resolve all template keys in config:\n%@", definitions);
    return nil;
  }

  NSLog(@"[Envoy] debug: config:\n%@", definitions);
  return definitions;
}

@end
