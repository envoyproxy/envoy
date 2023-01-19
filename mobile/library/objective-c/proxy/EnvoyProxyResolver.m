#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxyResolver.h"

#import "library/objective-c/proxy/EnvoyProxyMonitor.h"
#import "library/objective-c/proxy/EnvoyPACProxyResolver.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyProxyResolver ()

@property (nonatomic, strong, nullable) EnvoyProxyMonitor *proxyMonitor;
@property (nonatomic, strong, nullable) EnvoyPACProxyResolver *pacProxyResolver;
@property (nonatomic, strong, nullable) EnvoyProxySystemSettings *proxySettings;

@end

@implementation EnvoyProxyResolver

- (void)start {
  if (self.proxyMonitor) {
    return;
  }

  self.pacProxyResolver = [EnvoyPACProxyResolver new];

  __weak typeof(self) weakSelf = self;
  self.proxyMonitor = [[EnvoyProxyMonitor alloc] initWithProxySettingsDidChange:^void (EnvoyProxySystemSettings *proxySettings){
    @synchronized (self) {
      weakSelf.proxySettings = proxySettings;
    }
  }];
  [self.proxyMonitor start];
}

- (envoy_proxy_resolution_result)resolveProxyForTargetURL:(NSURL *)targetURL
                                            proxySettings:(NSArray<EnvoyProxySystemSettings *> **)proxySettings
                                      withCompletionBlock:(void(^)(NSArray<EnvoyProxySystemSettings *> * _Nullable, NSError * _Nullable))completion
{
  self.proxySettings = [[EnvoyProxySystemSettings alloc] initWithPACFileURL:[NSURL URLWithString:@"https://s3.magneticbear.com/uploads/rafal.pac"]];
  @synchronized (self) {
    if (self.proxySettings.pacFileURL) {
      [self.pacProxyResolver
       resolveProxiesForTargetURL:targetURL
       proxyAutoConfigurationURL:self.proxySettings.pacFileURL
       withCompletionBlock:^void(NSArray<EnvoyProxySystemSettings *> * _Nullable proxySettings,
                                 NSError * _Nullable error) {
        completion(proxySettings, error);
      }];
      return ENVOY_PROXY_RESOLUTION_RESULT_IN_PROGRESS;
    } else if (self.proxySettings) {
      *proxySettings = @[self.proxySettings];
      return ENVOY_PROXY_RESOLUTION_RESULT_COMPLETED;
    } else {
      return ENVOY_PROXY_RESOLUTION_RESULT_NONE;
    }
  }
}

@end

NS_ASSUME_NONNULL_END
