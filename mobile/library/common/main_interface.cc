#include "library/common/main_interface.h"

#include "common/upstream/logical_dns_cluster.h"

#include "exe/main_common.h"

#include "extensions/filters/http/router/config.h"
#include "extensions/filters/network/http_connection_manager/config.h"
#include "extensions/transport_sockets/raw_buffer/config.h"
#include "extensions/transport_sockets/tls/config.h"

// NOLINT(namespace-envoy)

/**
 * External entrypoint for library.
 */
extern "C" int run_envoy(const char* config) {
  std::unique_ptr<Envoy::MainCommon> main_common;

  char* envoy_argv[] = {strdup("envoy"), strdup("--config-yaml"), strdup(config), nullptr};

  // Ensure static factory registration occurs on time.
  Envoy::Extensions::HttpFilters::RouterFilter::forceRegisterRouterFilterConfig();
  Envoy::Extensions::NetworkFilters::HttpConnectionManager::
      forceRegisterHttpConnectionManagerFilterConfigFactory();
  Envoy::Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
  Envoy::Extensions::TransportSockets::RawBuffer::forceRegisterUpstreamRawBufferSocketFactory();
  Envoy::Extensions::TransportSockets::Tls::forceRegisterUpstreamSslSocketFactory();
  Envoy::Upstream::forceRegisterLogicalDnsClusterFactory();

  // Initialize the server's main context under a try/catch loop and simply
  // return EXIT_FAILURE as needed. Whatever code in the initialization path
  // that fails is expected to log an error message so the user can diagnose.
  try {
    main_common = std::make_unique<Envoy::MainCommon>(3, envoy_argv);
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    std::cerr << e.what() << std::endl;

    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected
  // exceptions show up as a core-dumps for easier diagnostics.
  return main_common->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
