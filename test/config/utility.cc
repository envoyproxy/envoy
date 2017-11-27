#include "test/config/utility.h"

#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/protobuf/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "api/filter/network/http_connection_manager.pb.h"

namespace Envoy {

const std::string basic_config = R"EOF(
static_resources:
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        config:
          http_filters:
            name: envoy.router
          codec_type: HTTP1
          route_config:
            virtual_hosts:
              name: integration
              routes:
                route:
                  cluster: cluster_0
                match:
                  prefix: "/"
              domains: "*"
            name: route_config_0
  clusters:
    name: cluster_0
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF";

const std::string ConfigHelper::DEFAULT_BUFFER_FILTER =
    R"EOF(
name: envoy.buffer
config:
  deprecated_v1: true
  value:
    max_request_bytes : 5242880
    max_request_time_s : 120
)EOF";

const std::string ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER =
    R"EOF(
name: envoy.health_check
config:
  deprecated_v1: true
  value:
    pass_through_mode: false
    endpoint: /healthcheck
)EOF";

ConfigHelper::ConfigHelper(const Network::Address::IpVersion version) {
  RELEASE_ASSERT(!finalized_);
  std::string filename =
      TestEnvironment::writeStringToFileForTest("basic_config.yaml", basic_config);
  MessageUtil::loadFromFile(filename, bootstrap_);

  // Fix up all the socket addresses with the correct version.
  auto* admin = bootstrap_.mutable_admin();
  auto* admin_socket_addr = admin->mutable_address()->mutable_socket_address();
  admin_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* static_resources = bootstrap_.mutable_static_resources();
  auto* listener = static_resources->mutable_listeners(0);
  auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
  listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* cluster = static_resources->mutable_clusters(0);
  auto host_socket_addr = cluster->mutable_hosts(0)->mutable_socket_address();
  host_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
}

void ConfigHelper::finalize(const std::vector<uint32_t>& ports) {
  RELEASE_ASSERT(!finalized_);
  for (auto config_modifier : config_modifiers_) {
    config_modifier(bootstrap_);
  }

  uint32_t port_idx = 0;
  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    for (int j = 0; j < cluster->hosts_size(); ++j) {
      if (cluster->mutable_hosts(j)->has_socket_address()) {
        auto* host_socket_addr = cluster->mutable_hosts(j)->mutable_socket_address();
        RELEASE_ASSERT(ports.size() >= port_idx);
        host_socket_addr->set_port_value(ports[port_idx++]);
      }
    }
  }
  ASSERT(port_idx == ports.size());

  if (!connect_timeout_set_) {
#ifdef __APPLE__
    // Set a high default connect timeout. Under heavy load (and in particular in CI), macOS
    // connections can take inordinately long to complete.
    setConnectTimeout(std::chrono::seconds(30));
#else
    // Set a default connect timeout.
    setConnectTimeout(std::chrono::seconds(5));
#endif
  }

  finalized_ = true;
}

void ConfigHelper::setSourceAddress(const std::string& address_string) {
  RELEASE_ASSERT(!finalized_);
  bootstrap_.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address(address_string);
}

void ConfigHelper::setDefaultHostAndRoute(const std::string& domains, const std::string& prefix) {
  RELEASE_ASSERT(!finalized_);
  envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* virtual_host = hcm_config.mutable_route_config()->mutable_virtual_hosts(0);
  virtual_host->set_domains(0, domains);
  virtual_host->mutable_routes(0)->mutable_match()->set_prefix(prefix);

  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::setBufferLimits(uint32_t upstream_buffer_limit,
                                   uint32_t downstream_buffer_limit) {
  RELEASE_ASSERT(!finalized_);
  RELEASE_ASSERT(bootstrap_.mutable_static_resources()->listeners_size() == 1);
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  listener->mutable_per_connection_buffer_limit_bytes()->set_value(downstream_buffer_limit);

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    cluster->mutable_per_connection_buffer_limit_bytes()->set_value(upstream_buffer_limit);
  }

  envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);
  if (hcm_config.codec_type() == envoy::api::v2::filter::network::HttpConnectionManager::HTTP2) {
    const uint32_t size =
        std::max(downstream_buffer_limit, Http::Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE);
    auto* options = hcm_config.mutable_http2_protocol_options();
    options->mutable_initial_stream_window_size()->set_value(size);
    storeHttpConnectionManager(hcm_config);
  }
}

void ConfigHelper::setConnectTimeout(std::chrono::milliseconds timeout) {
  RELEASE_ASSERT(!finalized_);

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    auto* connect_timeout = cluster->mutable_connect_timeout();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    connect_timeout->set_seconds(seconds.count());
    connect_timeout->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timeout - seconds).count());
  }
  connect_timeout_set_ = true;
}

void ConfigHelper::addRoute(const std::string& domains, const std::string& prefix,
                            const std::string& cluster,
                            envoy::api::v2::VirtualHost::TlsRequirementType type) {
  RELEASE_ASSERT(!finalized_);
  envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* virtual_host = hcm_config.mutable_route_config()->add_virtual_hosts();
  virtual_host->add_domains(domains);
  virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
  virtual_host->mutable_routes(0)->mutable_route()->set_cluster(cluster);
  virtual_host->set_require_tls(type);

  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::addFilter(const std::string& config) {
  RELEASE_ASSERT(!finalized_);
  envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* filter_list_back = hcm_config.add_http_filters();
  const std::string json = Json::Factory::loadFromYamlString(config)->asJsonString();
  MessageUtil::loadFromJson(json, *filter_list_back);

  // Now move it to the front.
  for (int i = hcm_config.http_filters_size() - 1; i > 0; --i) {
    hcm_config.mutable_http_filters()->SwapElements(i, i - 1);
  }
  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::setClientCodec(
    envoy::api::v2::filter::network::HttpConnectionManager::CodecType type) {
  RELEASE_ASSERT(!finalized_);
  envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);
  hcm_config.set_codec_type(type);
  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::addSslConfig() {
  RELEASE_ASSERT(!finalized_);

  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);

  auto* common_tls_context = filter_chain->mutable_tls_context()->mutable_common_tls_context();
  common_tls_context->add_alpn_protocols("h2");
  common_tls_context->add_alpn_protocols("http/1.1");
  common_tls_context->mutable_deprecated_v1()->set_alt_alpn_protocols("http/1.1");

  auto* validation_context = common_tls_context->mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  validation_context->add_verify_certificate_hash("9E:D6:53:36:49:26:9C:69:4F:71:7E:87:29:A1:C8:B5:"
                                                  "50:06:FD:51:01:3C:0E:0D:42:94:91:6E:00:D7:EA:"
                                                  "04");

  auto* tls_certificate = common_tls_context->add_tls_certificates();
  tls_certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/servercert.pem"));
  tls_certificate->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/serverkey.pem"));
}

void ConfigHelper::loadHttpConnectionManager(
    envoy::api::v2::filter::network::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_);
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  auto* filter_chain = listener->mutable_filter_chains(0);
  auto* hcm_config_struct = filter_chain->mutable_filters(0)->mutable_config();
  MessageUtil::jsonConvert(*hcm_config_struct, hcm);
}

void ConfigHelper::storeHttpConnectionManager(
    const envoy::api::v2::filter::network::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_);
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  auto* filter_chain = listener->mutable_filter_chains(0);
  auto* hcm_config_struct = filter_chain->mutable_filters(0)->mutable_config();

  MessageUtil::jsonConvert(hcm, *hcm_config_struct);
}

void ConfigHelper::addConfigModifier(ConfigModifierFunction function) {
  RELEASE_ASSERT(!finalized_);
  config_modifiers_.push_back(std::move(function));
}

void ConfigHelper::addConfigModifier(HttpModifierFunction function) {
  addConfigModifier([function, this](envoy::api::v2::Bootstrap&) -> void {
    envoy::api::v2::filter::network::HttpConnectionManager hcm_config;
    loadHttpConnectionManager(hcm_config);
    function(hcm_config);
    storeHttpConnectionManager(hcm_config);
  });
}

} // namespace Envoy
