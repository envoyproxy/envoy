#import <Foundation/Foundation.h>

#import "library/objective-c/proxy/EnvoyPACProxyResolver.h"

@interface Wrapper : NSObject

@property (nonatomic, copy) void(^block)(NSArray * _Nullable, NSError * _Nullable);

@end

@implementation Wrapper

@end

//static void* retainWrapper(void *ptr) {
//    return NULL;
//}
//
//static void releaseWrapper(void *ptr) {
////    return NULL;
//}
//
////static void copyWrapper(void *ptr) {
//////    return NULL;
////}

@implementation EnvoyPACProxyResolver

- (void)resolveProxiesForTargetURL:(NSURL *)targetURL
         proxyAutoConfigurationURL:(NSURL *)proxyAutoConfigurationURL
               withCompletionBlock:(void(^)(NSArray<EnvoyProxySettings *> * _Nullable, NSError * _Nullable))completion
{
  CFURLRef cfTargetURL =
  CFURLCreateWithString(kCFAllocatorDefault,
                        (__bridge_retained CFStringRef)[targetURL absoluteString],
                        NULL);
  CFURLRef cfProxyAutoConfigurationURL =
  CFURLCreateWithString(
                        kCFAllocatorDefault,
                        (__bridge_retained CFStringRef)[proxyAutoConfigurationURL absoluteString],
                        NULL);
  Wrapper *wrapper = [Wrapper new];
  wrapper.block = completion;
//  CFStreamClientContext context = {0, (__bridge void*)wrapper, retainWrapper, releaseWrapper, NULL};
  CFStreamClientContext context = {0, (void *)CFBridgingRetain(wrapper), NULL, NULL, NULL};
  CFRunLoopSourceRef runLoopSource =
  CFNetworkExecuteProxyAutoConfigurationURL(
                                            cfProxyAutoConfigurationURL,
                                            cfTargetURL,
                                            proxyAutoConfigurationResultCallback,
                                            &context);

  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);
}

void proxyAutoConfigurationResultCallback(void *ptr, CFArrayRef cfProxies, CFErrorRef cfError) {
  Wrapper *wrapper = CFBridgingRelease(ptr);

  if (cfError != NULL) {
    NSError *error = (__bridge NSError *)cfError;
    NSLog(@"RAF: ERROR: %@", error.localizedDescription);
    wrapper.block(nil, (__bridge NSError *)cfError);
  } else if (cfProxies != NULL) {
    NSLog(@"RAF: PROXIES ");
    NSMutableArray<EnvoyProxySettings *> *proxies = [NSMutableArray new];
    NSUInteger count = CFArrayGetCount(cfProxies);
    for (NSUInteger i = 0; i < count; i++) {
      NSDictionary *current = (__bridge NSDictionary *)((CFDictionaryRef)CFArrayGetValueAtIndex(cfProxies, i));
      NSString *proxyType = current[(NSString *)kCFProxyTypeKey];
      NSLog(@"RAF: %@", current);

//      if (!CFEqual(proxyType, kCFProxyTypeAutoConfigurationURL)) {
//        [proxies addObject:[[EnvoyProxySettings alloc] initWithHost:<#(NSString *)#> port:<#(NSUInteger)#>]];
//      }
    }
  } else {
    NSLog(@"RAF: NO PROXIES ");
    wrapper.block(@[], nil);
  }
}

@end
