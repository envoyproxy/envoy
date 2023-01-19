#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"
#import "library/common/types/c_types.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyProxyResolver: NSObject

- (void)start;

- (envoy_proxy_resolution_result)resolveProxyForTargetURL:(NSURL *)targetURL
                                            proxySettings:(NSArray<EnvoyProxySettings *> *_Nullable*_Nullable)proxySettings
                                      withCompletionBlock:(void(^)(NSArray<EnvoyProxySettings *> * _Nullable, NSError * _Nullable))completion;

@end

NS_ASSUME_NONNULL_END
