#include "test/common/proxy/test_apple_pac_proxy_resolver.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <memory>

#include "library/common/network/proxy_settings.h"

namespace Envoy {
namespace Network {

namespace {

// Contains state that is passed into the run loop callback.
struct RunLoopSourceInfo {
  RunLoopSourceInfo(CFStreamClientContext* _context, const std::string& _host, const int _port)
      : context(_context), host(_host), port(_port) {}

  CFStreamClientContext* context;
  const std::string& host;
  const int port;
};

void runLoopCallback(void* info) {
  // Wrap in unique_ptr so we release the memory at the end of the function execution.
  std::unique_ptr<RunLoopSourceInfo> run_loop_info(static_cast<RunLoopSourceInfo*>(info));

  const void* keys[] = {kCFProxyTypeKey, kCFProxyHostNameKey, kCFProxyPortNumberKey};
  const void* values[] = {
      kCFProxyTypeHTTPS,
      CFStringCreateWithCString(kCFAllocatorDefault, run_loop_info->host.c_str(),
                                kCFStringEncodingUTF8),
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &run_loop_info->port)};
  const int num_pairs = sizeof(keys) / sizeof(CFStringRef);

  CFDictionaryRef settings_dict = CFDictionaryCreate(
      kCFAllocatorDefault, static_cast<const void**>(keys), static_cast<const void**>(values),
      num_pairs, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

  const void* objects[] = {settings_dict};
  CFArrayRef proxies = CFArrayCreate(kCFAllocatorDefault, static_cast<const void**>(objects),
                                     /*numValues=*/1, &kCFTypeArrayCallBacks);

  // Wrap in unique_ptr so we release the memory at the end of the function execution.
  std::unique_ptr<CFStreamClientContext> client_context(run_loop_info->context);

  // Invoke the proxy resolution callback that the ApplePacProxyResolver invokes.
  Network::proxyAutoConfigurationResultCallback(/*ptr=*/client_context->info, proxies,
                                                /*cf_error=*/nullptr);

  // Release the memory allocated above.
  CFRelease(proxies);
  CFRelease(settings_dict);
  // We don't want to release the first value, since it's a global constant.
  for (int i = 1; i < num_pairs; ++i) {
    CFRelease(values[i]);
  }
}

} // namespace

TestApplePacProxyResolver::TestApplePacProxyResolver(const std::string& host, const int port)
    : host_(host), port_(port) {}

CFRunLoopSourceRef TestApplePacProxyResolver::createPacUrlResolverSource(
    CFURLRef /*cf_proxy_autoconfiguration_file_url*/, CFURLRef /*cf_target_url*/,
    CFStreamClientContext* context) {
  auto run_loop_info = std::make_unique<RunLoopSourceInfo>(context, host_, port_);
  run_loop_context_ = std::make_unique<CFRunLoopSourceContext>(
      CFRunLoopSourceContext{/*version=*/0,
                             /*info=*/run_loop_info.release(),
                             /*retain=*/nullptr,
                             /*release=*/nullptr,
                             /*copyDescription=*/nullptr,
                             /*equal=*/nullptr,
                             /*hash=*/nullptr,
                             /*schedule=*/nullptr,
                             /*cancel=*/nullptr,
                             /*perform=*/runLoopCallback});

  auto run_loop_source =
      CFRunLoopSourceCreate(kCFAllocatorDefault, /*order=*/0, run_loop_context_.get());
  // There are no network events that get triggered to notify this test run loop source that
  // it should process a result, so we have to signal it manually.
  CFRunLoopSourceSignal(run_loop_source);

  return run_loop_source;
}

} // namespace Network
} // namespace Envoy
