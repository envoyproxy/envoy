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
envoy_status_t run_engine(const char* config, const char* log_level) {
  std::unique_ptr<Envoy::MainCommon> main_common;

  char* envoy_argv[] = {strdup("envoy"), strdup("--config-yaml"), strdup(config),
                        strdup("-l"),    strdup(log_level),       nullptr};

  // Ensure static factory registration occurs on time.
  // Envoy's static factory registration happens when main is run.
  // However, when compiled as a library, there is no guarantee that such registration will happen
  // before the names are needed.
  // The following calls ensure that registration happens before the entities are needed.
  // Note that as more registrations are needed, explicit initialization calls will need to be added
  // here.
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
  // Note that in the Android examples logging will not be seen.
  // This is a known problem, and will be addressed by:
  // https://github.com/lyft/envoy-mobile/issues/34
  try {
    main_common = std::make_unique<Envoy::MainCommon>(5, envoy_argv);
  } catch (const Envoy::NoServingException& e) {
    return ENVOY_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    std::cerr << e.what() << std::endl;
    return ENVOY_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    std::cerr << e.what() << std::endl;
    return ENVOY_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected
  // exceptions show up as a core-dumps for easier diagnostics.
  return main_common->run() ? ENVOY_SUCCESS : ENVOY_FAILURE;
}
