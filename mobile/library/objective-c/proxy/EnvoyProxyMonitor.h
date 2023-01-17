#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"

NS_ASSUME_NONNULL_BEGIN

// Monitors proxy settings changes in order to update Envoy proxy configuration.
@interface EnvoyProxyMonitor : NSObject

- (instancetype)initWithProxySettingsDidChange:(void(^)(EnvoyProxySettings *_Nullable))proxySettingsDidChange;

@property (nonatomic, copy, readonly) void (^proxySettingsDidChange)(EnvoyProxySettings *_Nullable);

// Starts the receiver. Actives the monitoring of system proxy settings.
- (void)start;

@end

NS_ASSUME_NONNULL_END
