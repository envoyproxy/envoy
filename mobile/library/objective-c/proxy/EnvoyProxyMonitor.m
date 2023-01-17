#import <CFNetwork/CFNetwork.h>

#import "library/objective-c/proxy/EnvoyProxyMonitor.h"

NSTimeInterval kProxySettingsRefreshRateSeconds = 7;


@interface EnvoyProxyMonitor ()

@property (nonatomic, strong) dispatch_source_t dispatchSource;
@property (nonatomic, strong) EnvoyProxySettings *proxySettings;
@property (nonatomic, copy) void (^proxySettingsDidChange)(EnvoyProxySettings *);


@end

@implementation EnvoyProxyMonitor

- (instancetype)initWithProxySettingsDidChange:(void (^)(EnvoyProxySettings *))proxySettingsDidChange {
  self = [super init];
  if (self) {
    self.proxySettingsDidChange = proxySettingsDidChange;
  }

  return self;
}


- (void)start {
  [self stop];

  self.dispatchSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                       dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
  dispatch_source_set_timer(self.dispatchSource,
                            dispatch_time(DISPATCH_TIME_NOW, 0),
                            (int64_t)(kProxySettingsRefreshRateSeconds * NSEC_PER_SEC), 0);

  __weak typeof(self) weakSelf = self;
  dispatch_source_set_event_handler(self.dispatchSource, ^{
    [weakSelf pollProxySettings];
  });

  dispatch_resume(self.dispatchSource);
}

- (void)stop {
  if (self.dispatchSource != nil) {
    dispatch_suspend(self.dispatchSource);
    self.dispatchSource = nil;
  }
}

- (void)deinit {
  [self stop];
}

- (void)setProxySettings:(EnvoyProxySettings *)proxySettings {
  if ([self.proxySettings isEqual:proxySettings]) {
    return;
  }

  _proxySettings = proxySettings;
  self.proxySettingsDidChange(proxySettings);
}

#pragma mark - Private

- (void)pollProxySettings {
  NSDictionary *settings = (NSDictionary *) CFBridgingRelease(CFNetworkCopySystemProxySettings());
  BOOL isHTTPProxyEnabled = [settings[(NSString *)kCFNetworkProxiesHTTPEnable] intValue] > 0;
  BOOL isAutoConfigProxyEnabled = [settings[(NSString *)kCFNetworkProxiesProxyAutoConfigEnable] intValue] > 0;

  if (NO && isHTTPProxyEnabled) {
    NSString *host = settings[(NSString *)kCFNetworkProxiesHTTPProxy];
    NSUInteger port = [settings[(NSString *)kCFNetworkProxiesHTTPPort] unsignedIntValue];
      self.proxySettings = [[EnvoyProxySettings alloc] initWithHostname:host port:port];
  } else if (isAutoConfigProxyEnabled) {
    NSString *urlString = settings[(NSString *)kCFNetworkProxiesProxyAutoConfigURLString];
      // TODO: what to do with an incorrect string in here
      NSURL *url = [NSURL URLWithString:urlString];
      self.proxySettings = [[EnvoyProxySettings alloc] initWithPACFileURL:@"https://s3.magneticbear.com/uploads/rafal.pac"];


  } else {
    self.proxySettings = nil;
  }
}

@end
