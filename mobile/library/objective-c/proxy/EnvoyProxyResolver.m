#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxyResolver.h"

#import "library/objective-c/proxy/EnvoyProxyMonitor.h"
#import "library/objective-c/proxy/EnvoyPACProxyResolver.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyProxyResolver ()

@property (nonatomic, strong, nullable) EnvoyProxyMonitor *proxyMonitor;
@property (nonatomic, strong, nullable) EnvoyPACProxyResolver *pacProxyResolver;
@property (nonatomic, strong, nullable) EnvoyProxySettings *proxySettings;

@end

@implementation EnvoyProxyResolver

- (void)start {
    if (self.proxyMonitor) {
        return;
    }

    self.pacProxyResolver = [EnvoyPACProxyResolver new];

    __weak typeof(self) weakSelf = self;
    self.proxyMonitor = [[EnvoyProxyMonitor alloc] initWithProxySettingsDidChange:^void (EnvoyProxySettings *proxySettings){
        @synchronized (self) {
            weakSelf.proxySettings = proxySettings;
        }
    }];
    [self.proxyMonitor start];
}

- (void)resolveProxyForTargetURL:(NSURL *)targetURL
                            port:(uint16_t)port
             withCompletionBlock:(void(^)(NSArray<EnvoyProxySettings *> * _Nullable, NSError * _Nullable))completion
{
    @synchronized (self) {
        if (self.proxySettings.pacFileURL) {
            [self.pacProxyResolver
             resolveProxiesForTargetURL:targetURL
             proxyAutoConfigurationURL:self.proxySettings.pacFileURL
             withCompletionBlock:^void(NSArray<EnvoyProxySettings *> * _Nullable proxySettings, NSError * _Nullable error) {
                completion(proxySettings, error);
            }];
        } else {
            completion(@[self.proxySettings], nil);
        }
    }
}

@end

NS_ASSUME_NONNULL_END
