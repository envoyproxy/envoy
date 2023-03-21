#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"
#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

// Encapsulates logic responsible for resolving proxies using currently active system proxy settings.
@interface EnvoyProxyResolver : NSObject

// Starts proxy resolver. It needs to be called prior to any proxy resolution attempt.
- (void)start;

// Resolve proxy for a given target URL with the use of the currently active system proxy settings.
//
// @param targetURL: The URL for which the proxy should be resolved.
// @param proxySettings: An in-out type of a parameter that's used to pass the results
// @param completion: A closure that's called in an async manner for cases when a proxy resolution cannot be
//                    performed in a sync manner. The method returns
//                    `ENVOY_PROXY_RESOLUTION_RESULT_IN_PROGRESS` to let the caller know that the resolution
//                    result will be delivered in an async manner.
- (envoy_proxy_resolution_result)
    resolveProxyForTargetURL:(NSURL *)targetURL
               proxySettings:(NSArray<EnvoyProxySettings *> *_Nullable *_Nullable)proxySettings
         withCompletionBlock:
             (void (^)(NSArray<EnvoyProxySettings *> *_Nullable, NSError *_Nullable))completion;

@end

NS_ASSUME_NONNULL_END
