#include "library/objective-c/proxy/EnvoyProxySettings.h"

NS_ASSUME_NONNULL_BEGIN

typedef void (^EnvoyPacProxyResolverCompletionBlock)(NSArray<EnvoyProxySettings *> *_Nullable,
                                                     NSError *_Nullable);

@interface EnvoyPACProxyResolver : NSObject

- (void)resolveProxiesForTargetURL:(NSURL *)targetURL
         proxyAutoConfigurationURL:(NSURL *)proxyAutoConfigurationURL
               withCompletionBlock:(EnvoyPacProxyResolverCompletionBlock)completion;
@end

NS_ASSUME_NONNULL_END
