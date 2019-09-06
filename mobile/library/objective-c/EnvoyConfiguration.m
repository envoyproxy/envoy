#import "library/objective-c/EnvoyEngine.h"

#import "library/common/main_interface.h"

@implementation EnvoyConfiguration

- (instancetype)initWithConnectTimeoutSeconds:(UInt32)connectTimeoutSeconds
                            dnsRefreshSeconds:(UInt32)dnsRefreshSeconds
                            statsFlushSeconds:(UInt32)statsFlushSeconds {
  self = [super init];
  if (!self) {
    return nil;
  }

  self.connectTimeoutSeconds = connectTimeoutSeconds;
  self.dnsRefreshSeconds = dnsRefreshSeconds;
  self.statsFlushSeconds = statsFlushSeconds;
  return self;
}

- (nullable NSString *)resolveTemplate:(NSString *)templateYAML {
  NSDictionary<NSString *, NSString *> *templateKeysToValues = @{
    @"connect_timeout" : [NSString stringWithFormat:@"%is", self.connectTimeoutSeconds],
    @"dns_refresh_rate" : [NSString stringWithFormat:@"%is", self.dnsRefreshSeconds],
    @"stats_flush_interval" : [NSString stringWithFormat:@"%is", self.statsFlushSeconds]
  };
  for (NSString *templateKey in templateKeysToValues) {
    NSString *keyToReplace = [NSString stringWithFormat:@"{{ %@ }}", templateKey];
    templateYAML =
        [templateYAML stringByReplacingOccurrencesOfString:keyToReplace
                                                withString:templateKeysToValues[templateKey]];
  }

  if ([templateYAML rangeOfString:@"{{"].length != 0) {
    return nil;
  }

  return templateYAML;
}

@end
