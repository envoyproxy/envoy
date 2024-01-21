#import "test/objective-c/EnvoyTestApi.h"

#import "test/common/proxy/test_apple_api_registration.h"

@implementation EnvoyTestApi

+ (void)registerTestProxyResolver:(NSString*)host port:(NSInteger)port {
  register_test_apple_proxy_resolver([host UTF8String], port);
}

@end
