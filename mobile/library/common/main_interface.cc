#include "library/common/main_interface.h"

#include <unordered_map>

#include "common/upstream/logical_dns_cluster.h"

#include "exe/main_common.h"

#include "extensions/filters/http/router/config.h"
#include "extensions/filters/network/http_connection_manager/config.h"
#include "extensions/transport_sockets/raw_buffer/config.h"
#include "extensions/transport_sockets/tls/config.h"

#include "library/common/buffer/utility.h"
#include "library/common/http/dispatcher.h"
#include "library/common/http/header_utility.h"

// NOLINT(namespace-envoy)

static std::unique_ptr<Envoy::MainCommon> main_common_;
static std::unique_ptr<Envoy::Http::Dispatcher> http_dispatcher_;

envoy_stream start_stream(envoy_observer observer) {
  return {ENVOY_SUCCESS, http_dispatcher_->startStream(observer)};
}

envoy_status_t send_headers(envoy_stream_t stream_id, envoy_headers headers, bool end_stream) {
  return http_dispatcher_->sendHeaders(stream_id, headers, end_stream);
}

// TODO: implement.
envoy_status_t send_data(envoy_stream_t, envoy_data, bool) { return ENVOY_FAILURE; }
envoy_status_t send_metadata(envoy_stream_t, envoy_headers, bool) { return ENVOY_FAILURE; }
envoy_status_t send_trailers(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }
envoy_status_t locally_close_stream(envoy_stream_t) { return ENVOY_FAILURE; }
envoy_status_t reset_stream(envoy_stream_t) { return ENVOY_FAILURE; }

/*
 * Setup envoy for interaction via the main interface.
 */
void setup_envoy() {
  http_dispatcher_ = std::make_unique<Envoy::Http::Dispatcher>(
      main_common_->server()->dispatcher(), main_common_->server()->clusterManager());
}

/**
 * External entrypoint for library.
 */
envoy_status_t run_engine(const char* config, const char* log_level) {
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
    main_common_ = std::make_unique<Envoy::MainCommon>(5, envoy_argv);
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
  return main_common_->run() ? ENVOY_SUCCESS : ENVOY_FAILURE;
}
