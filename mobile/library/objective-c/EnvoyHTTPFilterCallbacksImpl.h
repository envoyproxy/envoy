#pragma once

#import <Foundation/Foundation.h>

#import "library/objective-c/EnvoyEngine.h"

#import "library/common/extensions/filters/http/platform_bridge/c_types.h"

// Concrete implementation of the `EnvoyHTTPFilterCallbacks` protocol.
@interface EnvoyHTTPFilterCallbacksImpl : NSObject <EnvoyHTTPFilterCallbacks>

- (instancetype)initWithCallbacks:(envoy_http_filter_callbacks)callbacks;

@end
