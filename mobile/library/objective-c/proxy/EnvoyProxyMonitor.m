#import <CFNetwork/CFNetwork.h>

#import "library/objective-c/proxy/EnvoyProxyMonitor.h"

// The interval at which system proxy settings should be polled at.
NSTimeInterval kProxySettingsRefreshRateSeconds = 7;

@interface EnvoyProxyMonitor ()

@property (nonatomic, strong) dispatch_source_t dispatchSource;
@property (nonatomic, strong) EnvoyProxySystemSettings *proxySettings;
@property (nonatomic, copy) EnvoyProxyMonitorUpdate proxySettingsDidChange;
@property (nonatomic, assign) BOOL isStarted;

@end

@implementation EnvoyProxyMonitor

- (instancetype)initWithProxySettingsDidChange:(EnvoyProxyMonitorUpdate)proxySettingsDidChange {
  self = [super init];
  if (self) {
    self.proxySettingsDidChange = proxySettingsDidChange;
  }

  return self;
}

- (void)start {
  if (self.isStarted) {
    return;
  }

  self.isStarted = true;
  [self stop];

  self.dispatchSource =
      dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                             dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
  dispatch_source_set_timer(self.dispatchSource, dispatch_time(DISPATCH_TIME_NOW, 0),
                            (int64_t)(kProxySettingsRefreshRateSeconds * NSEC_PER_SEC), 0);

  __block BOOL isInitialUpdate = YES;
  __weak typeof(self) weakSelf = self;
  dispatch_source_set_event_handler(self.dispatchSource, ^{
    [weakSelf pollProxySettings:isInitialUpdate];
    isInitialUpdate = NO;
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

- (void)updateProxySettings:(EnvoyProxySystemSettings *)proxySettings force:(BOOL)force {
  if (!force) {
    if (self.proxySettings == nil && proxySettings == nil) {
      return;
    }

    if ([self.proxySettings isEqual:proxySettings]) {
      return;
    }
  }

  _proxySettings = proxySettings;
  self.proxySettingsDidChange(proxySettings);
}

#pragma mark - Private

- (void)pollProxySettings:(BOOL)forceUpdate {
  NSDictionary *settings = (NSDictionary *)CFBridgingRelease(CFNetworkCopySystemProxySettings());
  BOOL isHTTPProxyEnabled = [settings[(NSString *)kCFNetworkProxiesHTTPEnable] intValue] > 0;
  BOOL isAutoConfigProxyEnabled =
      [settings[(NSString *)kCFNetworkProxiesProxyAutoConfigEnable] intValue] > 0;

  if (isHTTPProxyEnabled) {
    NSString *host = settings[(NSString *)kCFNetworkProxiesHTTPProxy];
    NSUInteger port = [settings[(NSString *)kCFNetworkProxiesHTTPPort] unsignedIntValue];
    EnvoyProxySystemSettings *settings = [[EnvoyProxySystemSettings alloc] initWithHost:host
                                                                                   port:port];
    [self updateProxySettings:settings force:forceUpdate];

  } else if (isAutoConfigProxyEnabled) {
    NSString *urlString = settings[(NSString *)kCFNetworkProxiesProxyAutoConfigURLString];
    NSURL *url = [NSURL URLWithString:urlString];
    if (url) {
      // TODO: is ignoring the string which are invalid URLs a right thing to do in here?
      [self updateProxySettings:nil force:forceUpdate];
    } else {
      EnvoyProxySystemSettings *settings =
          [[EnvoyProxySystemSettings alloc] initWithPACFileURL:url];
      [self updateProxySettings:settings force:forceUpdate];
    }

  } else {
    [self updateProxySettings:nil force:forceUpdate];
  }
}

@end
