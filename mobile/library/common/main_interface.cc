#include "library/common/main_interface.h"

#include <atomic>
#include <unordered_map>

#include "common/upstream/logical_dns_cluster.h"

#include "exe/main_common.h"

#include "extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "extensions/filters/http/dynamic_forward_proxy/config.h"
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
static std::atomic<envoy_stream_t> current_stream_handle_{0};

envoy_stream_t init_stream(envoy_engine_t) { return current_stream_handle_++; }

envoy_status_t start_stream(envoy_stream_t stream, envoy_observer observer) {
  http_dispatcher_->startStream(stream, observer);
  return ENVOY_SUCCESS;
}

envoy_status_t send_headers(envoy_stream_t stream, envoy_headers headers, bool end_stream) {
  return http_dispatcher_->sendHeaders(stream, headers, end_stream);
}

// TODO: implement.
envoy_status_t send_data(envoy_stream_t, envoy_data, bool) { return ENVOY_FAILURE; }
envoy_status_t send_metadata(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }
envoy_status_t send_trailers(envoy_stream_t, envoy_headers) { return ENVOY_FAILURE; }

envoy_status_t reset_stream(envoy_stream_t stream) { return http_dispatcher_->resetStream(stream); }

envoy_engine_t init_engine() {
  // TODO(goaway): return new handle once multiple engine support is in place.
  // https://github.com/lyft/envoy-mobile/issues/332
  return 1;
}

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
  Envoy::Extensions::Clusters::DynamicForwardProxy::forceRegisterClusterFactory();
  Envoy::Extensions::HttpFilters::DynamicForwardProxy::
      forceRegisterDynamicForwardProxyFilterFactory();
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
