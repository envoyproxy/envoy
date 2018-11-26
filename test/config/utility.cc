#include "test/config/utility.h"

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/transport_socket/capture/v2alpha/capture.pb.h"
#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

const std::string ConfigHelper::BASE_CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
)EOF";

const std::string ConfigHelper::TCP_PROXY_CONFIG = BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        name: envoy.tcp_proxy
        config:
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF";

const std::string ConfigHelper::HTTP_PROXY_CONFIG = BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        config:
          stat_prefix: config_test
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
)EOF";

const std::string ConfigHelper::DEFAULT_BUFFER_FILTER =
    R"EOF(
name: envoy.buffer
config:
    max_request_bytes : 5242880
    max_request_time : 120s
)EOF";

const std::string ConfigHelper::SMALL_BUFFER_FILTER =
    R"EOF(
name: envoy.buffer
config:
    max_request_bytes : 1024
    max_request_time : 5s
)EOF";

const std::string ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER =
    R"EOF(
name: envoy.health_check
config:
    pass_through_mode: false
)EOF";

const std::string ConfigHelper::DEFAULT_SQUASH_FILTER =
    R"EOF(
name: envoy.squash
config:
  cluster: squash
  attachment_template:
    spec:
      attachment:
        env: "{{ SQUASH_ENV_TEST }}"
      match_request: true
  attachment_timeout:
    seconds: 1
    nanos: 0
  attachment_poll_period:
    seconds: 2
    nanos: 0
  request_timeout:
    seconds: 1
    nanos: 0
)EOF";

ConfigHelper::ConfigHelper(const Network::Address::IpVersion version, const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  std::string filename = TestEnvironment::writeStringToFileForTest("basic_config.yaml", config);
  MessageUtil::loadFromFile(filename, bootstrap_);

  // Fix up all the socket addresses with the correct version.
  auto* admin = bootstrap_.mutable_admin();
  auto* admin_socket_addr = admin->mutable_address()->mutable_socket_address();
  admin_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < static_resources->listeners_size(); ++i) {
    auto* listener = static_resources->mutable_listeners(i);
    auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
    listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
  }

  for (int i = 0; i < static_resources->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (!cluster->hosts().empty() && cluster->mutable_hosts(0)->has_socket_address()) {
      auto host_socket_addr = cluster->mutable_hosts(0)->mutable_socket_address();
      host_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
    }
  }
}

void ConfigHelper::finalize(const std::vector<uint32_t>& ports) {
  RELEASE_ASSERT(!finalized_, "");
  for (auto config_modifier : config_modifiers_) {
    config_modifier(bootstrap_);
  }

  uint32_t port_idx = 0;
  bool eds_hosts = false;
  auto* static_resources = bootstrap_.mutable_static_resources();
  const auto capture_path = TestEnvironment::getOptionalEnvVar("CAPTURE_PATH");
  if (capture_path) {
    ENVOY_LOG_MISC(debug, "Test capture path set to {}", capture_path.value());
  } else {
    ENVOY_LOG_MISC(debug, "No capture path set for tests");
  }
  for (int i = 0; i < bootstrap_.mutable_static_resources()->listeners_size(); ++i) {
    auto* listener = static_resources->mutable_listeners(i);
    for (int j = 0; j < listener->filter_chains_size(); ++j) {
      if (capture_path) {
        auto* filter_chain = listener->mutable_filter_chains(j);
        const bool has_tls = filter_chain->has_tls_context();
        absl::optional<ProtobufWkt::Struct> tls_config;
        if (has_tls) {
          tls_config = ProtobufWkt::Struct();
          MessageUtil::jsonConvert(filter_chain->tls_context(), tls_config.value());
          filter_chain->clear_tls_context();
        }
        setCaptureTransportSocket(capture_path.value(), fmt::format("listener_{}_{}", i, j),
                                  *filter_chain->mutable_transport_socket(), tls_config);
      }
    }
  }
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (cluster->type() == envoy::api::v2::Cluster::EDS) {
      eds_hosts = true;
    } else {
      for (int j = 0; j < cluster->hosts_size(); ++j) {
        if (cluster->mutable_hosts(j)->has_socket_address()) {
          auto* host_socket_addr = cluster->mutable_hosts(j)->mutable_socket_address();
          RELEASE_ASSERT(ports.size() > port_idx, "");
          host_socket_addr->set_port_value(ports[port_idx++]);
        }
      }

      // Assign ports to statically defined load_assignment hosts.
      for (int j = 0; j < cluster->load_assignment().endpoints_size(); ++j) {
        auto locality_lb = cluster->mutable_load_assignment()->mutable_endpoints(j);
        for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
          auto lb_endpoint = locality_lb->mutable_lb_endpoints(k);
          if (lb_endpoint->endpoint().address().has_socket_address()) {
            RELEASE_ASSERT(ports.size() > port_idx, "");
            lb_endpoint->mutable_endpoint()
                ->mutable_address()
                ->mutable_socket_address()
                ->set_port_value(ports[port_idx++]);
          }
        }
      }
    }

    if (capture_path) {
      const bool has_tls = cluster->has_tls_context();
      absl::optional<ProtobufWkt::Struct> tls_config;
      if (has_tls) {
        tls_config = ProtobufWkt::Struct();
        MessageUtil::jsonConvert(cluster->tls_context(), tls_config.value());
        cluster->clear_tls_context();
      }
      setCaptureTransportSocket(capture_path.value(), fmt::format("cluster_{}", i),
                                *cluster->mutable_transport_socket(), tls_config);
    }
  }
  ASSERT(port_idx == ports.size() || eds_hosts);

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

void ConfigHelper::setCaptureTransportSocket(
    const std::string& capture_path, const std::string& type,
    envoy::api::v2::core::TransportSocket& transport_socket,
    const absl::optional<ProtobufWkt::Struct>& tls_config) {
  // Determine inner transport socket.
  envoy::api::v2::core::TransportSocket inner_transport_socket;
  if (!transport_socket.name().empty()) {
    RELEASE_ASSERT(!tls_config, "");
    inner_transport_socket.MergeFrom(transport_socket);
  } else if (tls_config.has_value()) {
    inner_transport_socket.set_name("tls");
    inner_transport_socket.mutable_config()->MergeFrom(tls_config.value());
  } else {
    inner_transport_socket.set_name("raw_buffer");
  }
  // Configure outer capture transport socket.
  transport_socket.set_name("envoy.transport_sockets.capture");
  envoy::config::transport_socket::capture::v2alpha::Capture capture_config;
  auto* file_sink = capture_config.mutable_file_sink();
  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  const std::string test_id =
      std::string(test_info->name()) + "_" + std::string(test_info->test_case_name()) + "_" + type;
  file_sink->set_path_prefix(capture_path + "_" + absl::StrReplaceAll(test_id, {{"/", "_"}}));
  file_sink->set_format(envoy::config::transport_socket::capture::v2alpha::FileSink::PROTO_TEXT);
  capture_config.mutable_transport_socket()->MergeFrom(inner_transport_socket);
  MessageUtil::jsonConvert(capture_config, *transport_socket.mutable_config());
}

void ConfigHelper::setSourceAddress(const std::string& address_string) {
  RELEASE_ASSERT(!finalized_, "");
  bootstrap_.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_address(address_string);
  // We don't have the ability to bind to specific ports yet.
  bootstrap_.mutable_cluster_manager()
      ->mutable_upstream_bind_config()
      ->mutable_source_address()
      ->set_port_value(0);
}

void ConfigHelper::setDefaultHostAndRoute(const std::string& domains, const std::string& prefix) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* virtual_host = hcm_config.mutable_route_config()->mutable_virtual_hosts(0);
  virtual_host->set_domains(0, domains);
  virtual_host->mutable_routes(0)->mutable_match()->set_prefix(prefix);

  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::setBufferLimits(uint32_t upstream_buffer_limit,
                                   uint32_t downstream_buffer_limit) {
  RELEASE_ASSERT(!finalized_, "");
  RELEASE_ASSERT(bootstrap_.mutable_static_resources()->listeners_size() == 1, "");
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  listener->mutable_per_connection_buffer_limit_bytes()->set_value(downstream_buffer_limit);

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    cluster->mutable_per_connection_buffer_limit_bytes()->set_value(upstream_buffer_limit);
  }

  auto filter = getFilterFromListener("envoy.http_connection_manager");
  if (filter) {
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
    loadHttpConnectionManager(hcm_config);
    if (hcm_config.codec_type() ==
        envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::HTTP2) {
      const uint32_t size =
          std::max(downstream_buffer_limit, Http::Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE);
      auto* options = hcm_config.mutable_http2_protocol_options();
      options->mutable_initial_stream_window_size()->set_value(size);
      storeHttpConnectionManager(hcm_config);
    }
  }
}

void ConfigHelper::setConnectTimeout(std::chrono::milliseconds timeout) {
  RELEASE_ASSERT(!finalized_, "");

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
                            const std::string& cluster, bool validate_clusters,
                            envoy::api::v2::route::RouteAction::ClusterNotFoundResponseCode code,
                            envoy::api::v2::route::VirtualHost::TlsRequirementType type,
                            envoy::api::v2::route::RouteAction::RetryPolicy retry_policy,
                            bool include_attempt_count_header, const absl::string_view upgrade) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* route_config = hcm_config.mutable_route_config();
  route_config->mutable_validate_clusters()->set_value(validate_clusters);
  auto* virtual_host = route_config->add_virtual_hosts();
  virtual_host->set_name(domains);
  virtual_host->set_include_request_attempt_count(include_attempt_count_header);
  virtual_host->add_domains(domains);
  virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
  auto* route = virtual_host->mutable_routes(0)->mutable_route();
  route->set_cluster(cluster);
  route->set_cluster_not_found_response_code(code);
  route->mutable_retry_policy()->Swap(&retry_policy);
  if (!upgrade.empty()) {
    route->add_upgrade_configs()->set_upgrade_type(std::string(upgrade));
  }
  virtual_host->set_require_tls(type);

  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::addFilter(const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
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
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::CodecType
        type) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  if (loadHttpConnectionManager(hcm_config)) {
    hcm_config.set_codec_type(type);
    storeHttpConnectionManager(hcm_config);
  }
}

void ConfigHelper::addSslConfig() {
  RELEASE_ASSERT(!finalized_, "");

  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
  initializeTls(*filter_chain->mutable_tls_context()->mutable_common_tls_context());
}

void ConfigHelper::initializeTls(envoy::api::v2::auth::CommonTlsContext& common_tls_context) {
  common_tls_context.add_alpn_protocols("h2");
  common_tls_context.add_alpn_protocols("http/1.1");

  auto* validation_context = common_tls_context.mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

  auto* tls_certificate = common_tls_context.add_tls_certificates();
  tls_certificate->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/servercert.pem"));
  tls_certificate->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("/test/config/integration/certs/serverkey.pem"));
}

void ConfigHelper::renameListener(const std::string& name) {
  auto* static_resources = bootstrap_.mutable_static_resources();
  if (static_resources->listeners_size() > 0) {
    static_resources->mutable_listeners(0)->set_name(name);
  }
}

envoy::api::v2::listener::Filter* ConfigHelper::getFilterFromListener(const std::string& name) {
  RELEASE_ASSERT(!finalized_, "");
  if (bootstrap_.mutable_static_resources()->listeners_size() == 0) {
    return nullptr;
  }
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  if (listener->filter_chains_size() == 0) {
    return nullptr;
  }
  auto* filter_chain = listener->mutable_filter_chains(0);
  for (ssize_t i = 0; i < filter_chain->filters_size(); i++) {
    if (filter_chain->mutable_filters(i)->name() == name) {
      return filter_chain->mutable_filters(i);
    }
  }
  return nullptr;
}

bool ConfigHelper::loadHttpConnectionManager(
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_, "");
  auto* hcm_filter = getFilterFromListener("envoy.http_connection_manager");
  if (hcm_filter) {
    MessageUtil::jsonConvert(*hcm_filter->mutable_config(), hcm);
    return true;
  }
  return false;
}

void ConfigHelper::storeHttpConnectionManager(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_, "");
  auto* hcm_config_struct =
      getFilterFromListener("envoy.http_connection_manager")->mutable_config();

  MessageUtil::jsonConvert(hcm, *hcm_config_struct);
}

void ConfigHelper::addConfigModifier(ConfigModifierFunction function) {
  RELEASE_ASSERT(!finalized_, "");
  config_modifiers_.push_back(std::move(function));
}

void ConfigHelper::addConfigModifier(HttpModifierFunction function) {
  addConfigModifier([function, this](envoy::config::bootstrap::v2::Bootstrap&) -> void {
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
    loadHttpConnectionManager(hcm_config);
    function(hcm_config);
    storeHttpConnectionManager(hcm_config);
  });
}

EdsHelper::EdsHelper() : eds_path_(TestEnvironment::writeStringToFileForTest("eds.pb_text", "")) {
  // cluster.cluster_0.update_success will be incremented on the initial
  // load when Envoy comes up.
  ++update_successes_;
}

void EdsHelper::setEds(
    const std::vector<envoy::api::v2::ClusterLoadAssignment>& cluster_load_assignments,
    IntegrationTestServerStats& server_stats) {
  // Write to file the DiscoveryResponse and trigger inotify watch.
  envoy::api::v2::DiscoveryResponse eds_response;
  eds_response.set_version_info(std::to_string(eds_version_++));
  eds_response.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
  for (const auto& cluster_load_assignment : cluster_load_assignments) {
    eds_response.add_resources()->PackFrom(cluster_load_assignment);
  }
  // Past the initial write, need move semantics to trigger inotify move event that the
  // FilesystemSubscriptionImpl is subscribed to.
  std::string path =
      TestEnvironment::writeStringToFileForTest("eds.update.pb_text", eds_response.DebugString());
  TestUtility::renameFile(path, eds_path_);
  // Make sure Envoy has consumed the update now that it is running.
  server_stats.waitForCounterGe("cluster.cluster_0.update_success", ++update_successes_);
  RELEASE_ASSERT(
      update_successes_ == server_stats.counter("cluster.cluster_0.update_success")->value(), "");
}

} // namespace Envoy
