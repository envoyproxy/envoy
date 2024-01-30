#import "test/objective-c/EnvoyTestApi.h"

#import "test/common/proxy/test_apple_api_registration.h"

@implementation EnvoyTestApi

+ (void)registerTestProxyResolver:(NSString *)host
                             port:(NSInteger)port
                   usePacResolver:(BOOL)usePacResolver {
  registerTestAppleProxyResolver([host UTF8String], port, usePacResolver);
}

@end
