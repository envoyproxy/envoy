#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyPACProxyResolver.h"

@interface Wrapper : NSObject

@property (nonatomic, copy) void(^block)(NSArray * _Nullable, NSError * _Nullable);

@end

@implementation Wrapper



@end

static void* retainWrapper(void *ptr) {
    return NULL;
}

static void releaseWrapper(void *ptr) {
//    return NULL;
}

//static void copyWrapper(void *ptr) {
////    return NULL;
//}

@implementation EnvoyPACProxyResolver

- (void)resolveProxiesForTargetURL:(NSURL *)targetURL
         proxyAutoConfigurationURL:(NSURL *)proxyAutoConfigurationURL
               withCompletionBlock:(void(^)(NSArray * _Nullable, NSError * _Nullable))completion
{
  CFURLRef cfProxyAutoConfigurationURL =
  CFURLCreateWithString(
                        kCFAllocatorDefault,
                        (__bridge_retained CFStringRef)proxyAutoConfigurationURL,
                        NULL);
  CFURLRef cfTargetURL =
  CFURLCreateWithString(kCFAllocatorDefault,
                        (__bridge_retained CFStringRef)targetURL,
                        NULL);

  Wrapper *wrapper = [Wrapper new];
  wrapper.block = completion;
  CFStreamClientContext context = {0, (__bridge void*)wrapper, retainWrapper, releaseWrapper, NULL};
  CFRunLoopSourceRef runLoopSource =
  CFNetworkExecuteProxyAutoConfigurationURL(
                                            cfProxyAutoConfigurationURL,
                                            cfTargetURL,
                                            proxyAutoConfigurationResultCallback,
                                            &context);

  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);
}

void proxyAutoConfigurationResultCallback(void *ptr, CFArrayRef cfProxies, CFErrorRef cfError) {
  NSError *error = (__bridge NSError *)cfError;
  NSLog(@"test %@", error);


}

@end
