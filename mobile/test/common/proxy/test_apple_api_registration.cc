#include "test/common/proxy/test_apple_api_registration.h"

#include "test/common/proxy/test_apple_proxy_settings_monitor.h"

#include "library/common/api/external.h"
#include "library/common/network/apple_proxy_resolution.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/network/proxy_api.h"
#include "library/common/network/proxy_resolver_interface.h"
#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

void register_test_apple_proxy_resolver(absl::string_view host, int port) {
  // Fetch the existing registered envoy_proxy_resolver API, if it exists.
  void* existing_proxy_resolver =
      Envoy::Api::External::retrieveApi("envoy_proxy_resolver", /*allow_absent=*/true);
  if (existing_proxy_resolver != nullptr) {
    std::unique_ptr<Envoy::Network::ProxyResolverApi> wrapped(
        static_cast<Envoy::Network::ProxyResolverApi*>(existing_proxy_resolver));
    // Delete the existing ProxyResolverApi.
    wrapped.reset();
  }

  // Create a new test AppleProxyResolver.
  auto test_resolver = std::make_unique<Envoy::Network::AppleProxyResolver>();
  // Create a TestAppleSystemProxySettingsMonitor and set the test resolver to use the
  // TestAppleSystemProxySettingsMonitor.
  test_resolver->setSettingsMonitorForTest(
      std::make_unique<Envoy::test::TestAppleSystemProxySettingsMonitor>(
          std::string(host), port, test_resolver->proxySettingsUpdater()));
  // Start the resolver, as we do when registering the envoy_proxy_resolver API.
  test_resolver->start();
  // Create a new test ProxyResolverApi.
  auto proxy_resolver = std::make_unique<Envoy::Network::ProxyResolverApi>();
  // Set the API to use the test AppleProxyResolver.
  proxy_resolver->resolver = std::move(test_resolver);
  // Register the new test ProxyResolverApi. The Api registry takes over the pointer.
  Envoy::Api::External::registerApi("envoy_proxy_resolver", proxy_resolver.release());
}
