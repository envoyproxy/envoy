#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

- (instancetype)initWithStatsDomain:(NSString *)statsDomain
              connectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                  dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                  statsFlushSeconds:(UInt32)statsFlushSeconds {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.statsDomain = statsDomain;
  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.statsFlushSeconds = statsFlushSeconds;
  return self;
}

- (nullable NSString *)resolveTemplate:(NSString *)templateYAML {
  NSDictionary<NSString *, NSString *> *templateKeysToValues = @{
    @"stats_domain" : self.statsDomain,
    @"connect_timeout_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.connectTimeoutSeconds],
    @"dns_refresh_rate_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.dnsRefreshSeconds],
    @"stats_flush_interval_seconds" :
        [NSString stringWithFormat:@"%lu", (unsigned long)self.statsFlushSeconds],
    @"device_os" : @"iOS"

  };

  for (NSString *templateKey in templateKeysToValues) {
    NSString *keyToReplace = [NSString stringWithFormat:@"{{ %@ }}", templateKey];
    templateYAML =
        [templateYAML stringByReplacingOccurrencesOfString:keyToReplace
                                                withString:templateKeysToValues[templateKey]];
  }

  if ([templateYAML rangeOfString:@"{{"].length != 0) {
    NSLog(@"Error: Could not resolve all configuration template keys");
    return nil;
  }

  return templateYAML;
}

@end
