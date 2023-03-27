#include "library/common/network/apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <dispatch/dispatch.h>

namespace Envoy {
namespace Network {

void ApplePACProxyResolver::resolveProxies(absl::string_view target_url_string, absl::string_view, std::function<void(std::vector<ProxySettings>)>) {
//  target_url_string.begin
  auto cf_target_url_string = CFStringCreateWithCString(kCFAllocatorDefault, target_url_string.begin(), kCFStringEncodingUTF8);
  auto cf_targtet_url = CFURLCreateWithString(kCFAllocatorDefault, cf_target_url_string, NULL);
  CFRelease(cf_target_url_string);



//  EnvoyPACProxyResolverResolutionCompletionCallbackWrapper *completionWrapper =
//      [EnvoyPACProxyResolverResolutionCompletionCallbackWrapper new];
//  completionWrapper.completion = completion;
//
//  CFStreamClientContext context = {0, NULL, NULL, NULL, NULL};
//  CFRunLoopSourceRef runLoopSource = CFNetworkExecuteProxyAutoConfigurationURL(
//      cfProxyAutoConfigurationURL, cf_targtet_url, proxyAutoConfigurationResultCallback, &context);
//
//  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);
}

}
}


//- (void)resolveProxiesForTargetURL:(NSURL *)targetURL
//         proxyAutoConfigurationURL:(NSURL *)proxyAutoConfigurationURL
//               withCompletionBlock:(EnvoyPacProxyResolverCompletionBlock)completion {
//  CFURLRef cfTargetURL = CFURLCreateWithString(
//      kCFAllocatorDefault, (__bridge_retained CFStringRef)[targetURL absoluteString], NULL);
//  CFURLRef cfProxyAutoConfigurationURL = CFURLCreateWithString(
//      kCFAllocatorDefault,
//      (__bridge_retained CFStringRef)[proxyAutoConfigurationURL absoluteString], NULL);
//  EnvoyPACProxyResolverResolutionCompletionCallbackWrapper *completionWrapper =
//      [EnvoyPACProxyResolverResolutionCompletionCallbackWrapper new];
//  completionWrapper.completion = completion;
//
//  CFStreamClientContext context = {0, (void *)CFBridgingRetain(completionWrapper), NULL, NULL,
//                                   NULL};
//  CFRunLoopSourceRef runLoopSource = CFNetworkExecuteProxyAutoConfigurationURL(
//      cfProxyAutoConfigurationURL, cfTargetURL, proxyAutoConfigurationResultCallback, &context);
//
//  CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);
//}
//
//void proxyAutoConfigurationResultCallback(void *ptr, CFArrayRef cfProxies, CFErrorRef cfError) {
//  EnvoyPACProxyResolverResolutionCompletionCallbackWrapper *completionWrapper =
//      CFBridgingRelease(ptr);
//
//  if (cfError != NULL) {
//    NSError *error = (__bridge NSError *)cfError;
//    NSLog(@"RAF: ERROR: %@", error.localizedDescription);
//    completionWrapper.completion(nil, (__bridge NSError *)cfError);
//
//  } else if (cfProxies != NULL) {
//    NSLog(@"RAF: PROXIES ");
//    NSMutableArray<EnvoyProxySettings *> *proxies = [NSMutableArray new];
//
//    for (NSUInteger i = 0; i < CFArrayGetCount(cfProxies); i++) {
//      NSDictionary *current =
//          (__bridge NSDictionary *)((CFDictionaryRef)CFArrayGetValueAtIndex(cfProxies, i));
//      NSString *proxyType = current[(NSString *)kCFProxyTypeKey];
//      NSLog(@"RAF: %@", current);
//
//      // Ignore kCFProxyTypeAutoConfigurationURL, kCFProxyTypeFTP and kCFProxyTypeSOCKS proxies.
//      if ([proxyType isEqualToString:(NSString *)kCFProxyTypeHTTP] ||
//          [proxyType isEqualToString:(NSString *)kCFProxyTypeHTTPS]) {
//        NSString *host = current[(NSString *)kCFProxyHostNameKey];
//        NSUInteger port = [current[(NSString *)kCFProxyPortNumberKey] unsignedIntegerValue];
//        [proxies addObject:[[EnvoyProxySettings alloc] initWithHost:host port:port]];
//
//      } else if ([proxyType isEqualToString:(NSString *)kCFProxyTypeNone]) {
//        [proxies addObject:[EnvoyProxySettings directProxy]];
//      }
//    }
//
//    completionWrapper.completion(proxies, nil);
//  } else {
//    NSLog(@"RAF: NO PROXIES ");
//    completionWrapper.completion(@[], nil);
//  }
//}
