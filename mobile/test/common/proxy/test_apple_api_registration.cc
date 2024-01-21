#include "test/common/proxy/test_apple_api_registration.h"

#include "test/common/proxy/test_apple_proxy_settings_monitor.h"

#include "library/common/api/external.h"
#include "library/common/network/apple_proxy_resolution.h"
#include "library/common/network/apple_proxy_resolver.h"
#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

void register_test_apple_proxy_resolver(absl::string_view host, int port) {
  // Fetch the existing registered envoy_proxy_resolver API.
  const envoy_proxy_resolver* proxy_resolver = static_cast<envoy_proxy_resolver*>(
      Envoy::Api::External::retrieveApiSafe("envoy_proxy_resolver"));
  // Create a new test AppleProxyResolver.
  auto test_resolver = std::make_unique<Envoy::Network::AppleProxyResolver>();
  // Create a TestAppleSystemProxySettingsMonitor and set the test resolver to use the
  // TestAppleSystemProxySettingsMonitor.
  test_resolver->setSettingsMonitorForTest(
      std::make_unique<Envoy::test::TestAppleSystemProxySettingsMonitor>(
          std::string(host), port, test_resolver->proxySettingsUpdater()));
  // Start the resolver, as we do when registering the envoy_proxy_resolver API.
  test_resolver->start();
  // Reset the envoy_proxy_resolver to use the test AppleProxyResolver.
  auto* context =
      static_cast<Envoy::Network::envoy_proxy_resolver_context*>(proxy_resolver->context);
  context->resolver = std::move(test_resolver);
}
