#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

- (instancetype)initWithStatsDomain:(NSString *)statsDomain
              connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                  dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
       dnsFailureRefreshSecondsBase:(UInt32)dnsFailureRefreshSecondsBase
        dnsFailureRefreshSecondsMax:(UInt32)dnsFailureRefreshSecondsMax
                  statsFlushSeconds:(UInt32)statsFlushSeconds
                         appVersion:(NSString *)appVersion
                              appId:(NSString *)appId
                    virtualClusters:(NSString *)virtualClusters {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.statsDomain = statsDomain;
  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.dnsFailureRefreshSecondsBase = dnsFailureRefreshSecondsBase;
  self.dnsFailureRefreshSecondsMax = dnsFailureRefreshSecondsMax;
  self.statsFlushSeconds = statsFlushSeconds;
  self.appVersion = appVersion;
  self.appId = appId;
  self.virtualClusters = virtualClusters;
  return self;
}

- (nullable NSString *)resolveTemplate:(NSString *)templateYAML {
  NSDictionary<NSString *, NSString *> *templateKeysToValues = @{
    @"stats_domain" : self.statsDomain,
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
    @"device_os" : @"iOS",
    @"app_version" : self.appVersion,
    @"app_id" : self.appId,
    @"virtual_clusters" : self.virtualClusters
  };

  for (NSString *templateKey in templateKeysToValues) {
    NSString *keyToReplace = [NSString stringWithFormat:@"{{ %@ }}", templateKey];
    templateYAML =
        [templateYAML stringByReplacingOccurrencesOfString:keyToReplace
                                                withString:templateKeysToValues[templateKey]];
  }

  if ([templateYAML rangeOfString:@"{{"].length != 0) {
    NSLog(@"[Envoy] error: could not resolve all template keys in config:\n%@", templateYAML);
    return nil;
  }

  return templateYAML;
}

@end
