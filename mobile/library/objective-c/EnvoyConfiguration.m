#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

- (instancetype)initWithGrpcStatsDomain:(nullable NSString *)grpcStatsDomain
                  connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                      dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
           dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
            dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                      statsFlushSeconds:(UInt32)statsFlushSeconds
               streamIdleTimeoutSeconds:(UInt32)streamIdleTimeoutSeconds
                             appVersion:(NSString *)appVersion
                                  appId:(NSString *)appId
                        virtualClusters:(NSString *)virtualClusters
                 directResponseMatchers:(NSString *)directResponseMatchers
                        directResponses:(NSString *)directResponses
                      nativeFilterChain:(NSArray<EnvoyNativeFilterConfig *> *)nativeFilterChain
                    platformFilterChain:
                        (NSArray<EnvoyHTTPFilterFactory *> *)httpPlatformFilterFactories
                        stringAccessors:
                            (NSDictionary<NSString *, EnvoyStringAccessor *> *)stringAccessors {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.grpcStatsDomain = grpcStatsDomain;
  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
  self.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
  self.statsFlushSeconds = statsFlushSeconds;
  self.streamIdleTimeoutSeconds = streamIdleTimeoutSeconds;
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
  NSString *platformFilterConfigChain = [[NSString alloc] init];
  NSString *platformFilterTemplate = [[NSString alloc] initWithUTF8String:platform_filter_template];
  for (EnvoyHTTPFilterFactory *filterFactory in self.httpPlatformFilterFactories) {
    NSString *platformFilterConfig =
        [platformFilterTemplate stringByReplacingOccurrencesOfString:@"{{ platform_filter_name }}"
                                                          withString:filterFactory.filterName];
    platformFilterConfigChain =
        [platformFilterConfigChain stringByAppendingString:platformFilterConfig];
  }

  NSString *nativeFilterConfigChain = [[NSString alloc] init];
  NSString *nativeFilterTemplate = [[NSString alloc] initWithUTF8String:native_filter_template];
  for (EnvoyNativeFilterConfig *filterConfig in self.nativeFilterChain) {
    NSString *nativeFilterConfig =
        [[nativeFilterTemplate stringByReplacingOccurrencesOfString:@"{{ native_filter_name }}"
                                                         withString:filterConfig.name]
            stringByReplacingOccurrencesOfString:@"{{ native_filter_typed_config }}"
                                      withString:filterConfig.typedConfig];
    nativeFilterConfigChain = [nativeFilterConfigChain stringByAppendingString:nativeFilterConfig];
  }

  // Some direct response templates need to be resolved first because they have nested content.
  BOOL hasDirectResponses = self.directResponses.length > 0;
  if (hasDirectResponses) {
    NSString *listenerTemplate =
        [[NSString alloc] initWithUTF8String:fake_remote_listener_template];
    listenerTemplate =
        [listenerTemplate stringByReplacingOccurrencesOfString:@"{{ direct_responses }}"
                                                    withString:self.directResponses];
    templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"{{ fake_remote_listener }}"
                                                           withString:listenerTemplate];
  } else {
    templateYAML = [templateYAML stringByReplacingOccurrencesOfString:@"{{ fake_remote_listener }}"
                                                           withString:@""];
  }

  NSDictionary<NSString *, NSString *> *templateKeysToValues = @{
    @"platform_filter_chain" : platformFilterConfigChain,
    @"stats_domain" : self.grpcStatsDomain != nil ? self.grpcStatsDomain : @"0.0.0.0",
    @"stats_sink" : self.grpcStatsDomain != nil
        ? [[NSString alloc] initWithUTF8String:grpc_stats_sink_template]
        : @"",
    @"connect_timeout_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.connectTimeoutSeconds],
    @"dns_refresh_rate_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.dnsRefreshSeconds],
    @"dns_failure_refresh_rate_seconds_base" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.dnsFailureRefreshSecondsBase],
    @"dns_failure_refresh_rate_seconds_max" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.dnsFailureRefreshSecondsMax],
    @"stats_flush_interval_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.statsFlushSeconds],
    @"stream_idle_timeout_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.streamIdleTimeoutSeconds],
    @"device_os" : @"iOS",
    @"app_version" : self.appVersion,
    @"app_id" : self.appId,
    @"virtual_clusters" : self.virtualClusters,
    @"direct_responses" : self.directResponses,
    @"native_filter_chain" : nativeFilterConfigChain,
    @"fake_remote_cluster" : hasDirectResponses
        ? [[NSString alloc] initWithUTF8String:fake_remote_cluster_template]
        : @"",
    @"fake_cluster_matchers" : hasDirectResponses ? self.directResponseMatchers : @"",
    @"route_reset_filter" : hasDirectResponses
        ? [[NSString alloc] initWithUTF8String:route_cache_reset_filter_template]
        : @"",
  };

  for (NSString *templateKey in templateKeysToValues) {
    NSString *keyToReplace = [NSString stringWithFormat:@"{{ %@ }}", templateKey];
    templateYAML =
        [templateYAML stringByReplacingOccurrencesOfString:keyToReplace
                                                withString:templateKeysToValues[templateKey]];
  }

  if ([templateYAML containsString:@"{{"]) {
    NSLog(@"[Envoy] error: could not resolve all template keys in config:\n%@", templateYAML);
    return nil;
  }

  return templateYAML;
}

@end
