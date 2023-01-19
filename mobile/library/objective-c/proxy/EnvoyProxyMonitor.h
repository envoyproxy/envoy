#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySystemSettings.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^EnvoyProxyMonitorUpdate)(EnvoyProxySystemSettings *_Nullable);

// Monitors system proxy settings changes.
@interface EnvoyProxyMonitor : NSObject

// Initializes a new instance of the receiver. Calls a provided closure every time it detects a change of
// system proxy settings.
//
// @param proxySettingsDidChange The closure to call every time system proxy settings change.
- (instancetype)initWithProxySettingsDidChange:(EnvoyProxyMonitorUpdate)proxySettingsDidChange;

// Starts the monitoring of system proxy settings.
- (void)start;

@end

NS_ASSUME_NONNULL_END
