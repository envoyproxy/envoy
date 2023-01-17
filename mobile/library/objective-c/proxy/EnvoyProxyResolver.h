#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyProxySettings.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyProxyResolver: NSObject

- (void)start;

- (void)resolveProxyForTargetURL:(NSURL *)targetURL
                            port:(uint16_t)port
             withCompletionBlock:(void(^)(NSArray<EnvoyProxySettings *> * _Nullable, NSError * _Nullable))completion;

@end

NS_ASSUME_NONNULL_END
