#include "test/config/utility.h"

#include "envoy/config/accesslog/v2/file.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/transport_socket/tap/v2alpha/tap.pb.h"
#include "envoy/http/codec.h"

#include "common/common/assert.h"
#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "test/config/integration/certs/client_ecdsacert_hash.h"
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
dynamic_resources:
  lds_config:
    path: /dev/null
static_resources:
  secrets:
  - name: "secret_static_0"
    tls_certificate:
      certificate_chain:
        inline_string: "DUMMY_INLINE_BYTES"
      private_key:
        inline_string: "DUMMY_INLINE_BYTES"
      password:
        inline_string: "DUMMY_INLINE_BYTES"
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

const std::string ConfigHelper::BASE_UDP_LISTENER_CONFIG = R"EOF(
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
        address: 0.0.0.0
        port_value: 0
        protocol: udp
)EOF";

const std::string ConfigHelper::TCP_PROXY_CONFIG = BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF";

const std::string ConfigHelper::HTTP_PROXY_CONFIG = BASE_CONFIG + R"EOF(
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
            name: envoy.router
          codec_type: HTTP1
          access_log:
            name: envoy.file_access_log
            filter:
              not_health_check_filter:  {}
            typed_config:
              "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
              path: /dev/null
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
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.buffer.v2.Buffer
    max_request_bytes : 5242880
)EOF";

const std::string ConfigHelper::SMALL_BUFFER_FILTER =
    R"EOF(
name: envoy.buffer
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.buffer.v2.Buffer
    max_request_bytes : 1024
)EOF";

const std::string ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER =
    R"EOF(
name: envoy.health_check
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.health_check.v2.HealthCheck
    pass_through_mode: false
)EOF";

const std::string ConfigHelper::DEFAULT_SQUASH_FILTER =
    R"EOF(
name: envoy.squash
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.squash.v2.Squash
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

// TODO(fredlas) set_node_on_first_message_only was true; the delta+SotW unification
//               work restores it here.
// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::discoveredClustersBootstrap(const std::string& api_type) {
  return fmt::format(
      R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  cds_config:
    api_config_source:
      api_type: {}
      grpc_services:
        envoy_grpc:
          cluster_name: my_cds_cluster
      set_node_on_first_message_only: false
static_resources:
  clusters:
  - name: my_cds_cluster
    http2_protocol_options: {{}}
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
    name: http
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
            name: envoy.router
          codec_type: HTTP2
          route_config:
            name: route_config_0
            validate_clusters: false
            virtual_hosts:
              name: integration
              routes:
              - route:
                  cluster: cluster_1
                match:
                  prefix: "/cluster1"
              - route:
                  cluster: cluster_2
                match:
                  prefix: "/cluster2"
              domains: "*"
)EOF",
      api_type);
}

// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::adsBootstrap(const std::string& api_type) {
  return fmt::format(
      R"EOF(
dynamic_resources:
  lds_config:
    ads: {{}}
  cds_config:
    ads: {{}}
  ads_config:
    api_type: {}
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout:
      seconds: 5
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {{}}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
      api_type);
}

envoy::api::v2::Cluster ConfigHelper::buildCluster(const std::string& name, int port,
                                                   const std::string& ip_version) {
  return TestUtility::parseYaml<envoy::api::v2::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: STATIC
      load_assignment:
        cluster_name: {}
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: {}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                     name, name, ip_version, port));
}

ConfigHelper::ConfigHelper(const Network::Address::IpVersion version, Api::Api& api,
                           const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  std::string filename = TestEnvironment::writeStringToFileForTest("basic_config.yaml", config);
  TestUtility::loadFromFile(filename, bootstrap_, api);

  // Fix up all the socket addresses with the correct version.
  auto* admin = bootstrap_.mutable_admin();
  auto* admin_socket_addr = admin->mutable_address()->mutable_socket_address();
  admin_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < static_resources->listeners_size(); ++i) {
    auto* listener = static_resources->mutable_listeners(i);
    auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
    if (listener_socket_addr->address() == "0.0.0.0" || listener_socket_addr->address() == "::") {
      listener_socket_addr->set_address(Network::Test::getAnyAddressString(version));
    } else {
      listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
    }
  }

  for (int i = 0; i < static_resources->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (!cluster->hosts().empty()) {
      for (int j = 0; j < cluster->hosts().size(); j++) {
        if (cluster->mutable_hosts(j)->has_socket_address()) {
          auto host_socket_addr = cluster->mutable_hosts(j)->mutable_socket_address();
          host_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
        }
      }
    }
    for (int j = 0; j < cluster->load_assignment().endpoints_size(); ++j) {
      auto locality_lb = cluster->mutable_load_assignment()->mutable_endpoints(j);
      for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
        auto lb_endpoint = locality_lb->mutable_lb_endpoints(k);
        if (lb_endpoint->endpoint().address().has_socket_address()) {
          lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
              Network::Test::getLoopbackAddressString(version));
        }
      }
    }
  }
}

void ConfigHelper::applyConfigModifiers() {
  for (const auto& config_modifier : config_modifiers_) {
    config_modifier(bootstrap_);
  }
  config_modifiers_.clear();
}

void ConfigHelper::finalize(const std::vector<uint32_t>& ports) {
  RELEASE_ASSERT(!finalized_, "");

  applyConfigModifiers();

  uint32_t port_idx = 0;
  bool eds_hosts = false;
  bool custom_cluster = false;
  bool original_dst_cluster = false;
  auto* static_resources = bootstrap_.mutable_static_resources();
  const auto tap_path = TestEnvironment::getOptionalEnvVar("TAP_PATH");
  if (tap_path) {
    ENVOY_LOG_MISC(debug, "Test tap path set to {}", tap_path.value());
  } else {
    ENVOY_LOG_MISC(debug, "No tap path set for tests");
  }
  for (int i = 0; i < bootstrap_.mutable_static_resources()->listeners_size(); ++i) {
    auto* listener = static_resources->mutable_listeners(i);
    for (int j = 0; j < listener->filter_chains_size(); ++j) {
      if (tap_path) {
        auto* filter_chain = listener->mutable_filter_chains(j);
        const bool has_tls = filter_chain->has_tls_context();
        absl::optional<ProtobufWkt::Struct> tls_config;
        if (has_tls) {
          tls_config = ProtobufWkt::Struct();
          TestUtility::jsonConvert(filter_chain->tls_context(), tls_config.value());
          filter_chain->clear_tls_context();
        }
        setTapTransportSocket(tap_path.value(), fmt::format("listener_{}_{}", i, j),
                              *filter_chain->mutable_transport_socket(), tls_config);
      }
    }
  }
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (cluster->type() == envoy::api::v2::Cluster::EDS) {
      eds_hosts = true;
    } else if (cluster->type() == envoy::api::v2::Cluster::ORIGINAL_DST) {
      original_dst_cluster = true;
    } else if (cluster->has_cluster_type()) {
      custom_cluster = true;
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

    if (tap_path) {
      const bool has_tls = cluster->has_tls_context();
      absl::optional<ProtobufWkt::Struct> tls_config;
      if (has_tls) {
        tls_config = ProtobufWkt::Struct();
        TestUtility::jsonConvert(cluster->tls_context(), tls_config.value());
        cluster->clear_tls_context();
      }
      setTapTransportSocket(tap_path.value(), fmt::format("cluster_{}", i),
                            *cluster->mutable_transport_socket(), tls_config);
    }
  }
  ASSERT(skip_port_usage_validation_ || port_idx == ports.size() || eds_hosts ||
         original_dst_cluster || custom_cluster || bootstrap_.dynamic_resources().has_cds_config());

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

void ConfigHelper::setTapTransportSocket(const std::string& tap_path, const std::string& type,
                                         envoy::api::v2::core::TransportSocket& transport_socket,
                                         const absl::optional<ProtobufWkt::Struct>& tls_config) {
  // Determine inner transport socket.
  envoy::api::v2::core::TransportSocket inner_transport_socket;
  if (!transport_socket.name().empty()) {
    RELEASE_ASSERT(!tls_config, "");
    inner_transport_socket.MergeFrom(transport_socket);
  } else if (tls_config.has_value()) {
    inner_transport_socket.set_name("envoy.transport_sockets.tls");
    inner_transport_socket.mutable_config()->MergeFrom(tls_config.value());
  } else {
    inner_transport_socket.set_name("envoy.transport_sockets.raw_buffer");
  }
  // Configure outer tap transport socket.
  transport_socket.set_name("envoy.transport_sockets.tap");
  envoy::config::transport_socket::tap::v2alpha::Tap tap_config;
  tap_config.mutable_common_config()
      ->mutable_static_config()
      ->mutable_match_config()
      ->set_any_match(true);
  auto* output_sink = tap_config.mutable_common_config()
                          ->mutable_static_config()
                          ->mutable_output_config()
                          ->mutable_sinks()
                          ->Add();
  output_sink->set_format(envoy::service::tap::v2alpha::OutputSink::PROTO_TEXT);
  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  const std::string test_id =
      std::string(test_info->name()) + "_" + std::string(test_info->test_case_name()) + "_" + type;
  output_sink->mutable_file_per_tap()->set_path_prefix(tap_path + "_" +
                                                       absl::StrReplaceAll(test_id, {{"/", "_"}}));
  tap_config.mutable_transport_socket()->MergeFrom(inner_transport_socket);
  transport_socket.mutable_typed_config()->PackFrom(tap_config);
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

void ConfigHelper::setDownstreamHttpIdleTimeout(std::chrono::milliseconds timeout) {
  addConfigModifier(
      [timeout](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->MergeFrom(
            ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout.count()));
      });
}

void ConfigHelper::setDownstreamMaxConnectionDuration(std::chrono::milliseconds timeout) {
  addConfigModifier(
      [timeout](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_max_connection_duration()->MergeFrom(
            ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout.count()));
      });
}

void ConfigHelper::setConnectTimeout(std::chrono::milliseconds timeout) {
  RELEASE_ASSERT(!finalized_, "");

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    cluster->mutable_connect_timeout()->MergeFrom(
        ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout.count()));
  }
  connect_timeout_set_ = true;
}

envoy::api::v2::route::VirtualHost
ConfigHelper::createVirtualHost(const char* domain, const char* prefix, const char* cluster) {
  envoy::api::v2::route::VirtualHost virtual_host;
  virtual_host.set_name(domain);
  virtual_host.add_domains(domain);
  virtual_host.add_routes()->mutable_match()->set_prefix(prefix);
  auto* route = virtual_host.mutable_routes(0)->mutable_route();
  route->set_cluster(cluster);
  return virtual_host;
}

void ConfigHelper::addVirtualHost(const envoy::api::v2::route::VirtualHost& vhost) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);
  auto route_config = hcm_config.mutable_route_config();
  auto* virtual_host = route_config->add_virtual_hosts();
  virtual_host->CopyFrom(vhost);
  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::addFilter(const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* filter_list_back = hcm_config.add_http_filters();
  const std::string json = Json::Factory::loadFromYamlString(config)->asJsonString();
  TestUtility::loadFromJson(json, *filter_list_back);

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

void ConfigHelper::addSslConfig(const ServerSslOptions& options) {
  RELEASE_ASSERT(!finalized_, "");

  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
  envoy::api::v2::auth::DownstreamTlsContext tls_context;
  initializeTls(options, *tls_context.mutable_common_tls_context());
  filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
}

bool ConfigHelper::setAccessLog(const std::string& filename) {
  if (getFilterFromListener("envoy.http_connection_manager") == nullptr) {
    return false;
  }
  // Replace /dev/null with a real path for the file access log.
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
  loadHttpConnectionManager(hcm_config);
  envoy::config::accesslog::v2::FileAccessLog access_log_config;
  access_log_config.set_path(filename);
  hcm_config.mutable_access_log(0)->mutable_typed_config()->PackFrom(access_log_config);
  storeHttpConnectionManager(hcm_config);
  return true;
}

void ConfigHelper::initializeTls(const ServerSslOptions& options,
                                 envoy::api::v2::auth::CommonTlsContext& common_tls_context) {
  common_tls_context.add_alpn_protocols("h2");
  common_tls_context.add_alpn_protocols("http/1.1");

  auto* validation_context = common_tls_context.mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  validation_context->add_verify_certificate_hash(
      options.expect_client_ecdsa_cert_ ? TEST_CLIENT_ECDSA_CERT_HASH : TEST_CLIENT_CERT_HASH);

  // We'll negotiate up to TLSv1.3 for the tests that care, but it really
  // depends on what the client sets.
  common_tls_context.mutable_tls_params()->set_tls_maximum_protocol_version(
      options.tlsv1_3_ ? envoy::api::v2::auth::TlsParameters::TLSv1_3
                       : envoy::api::v2::auth::TlsParameters::TLSv1_2);
  if (options.rsa_cert_) {
    auto* tls_certificate = common_tls_context.add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
  }
  if (options.ecdsa_cert_) {
    auto* tls_certificate = common_tls_context.add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsacert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsakey.pem"));
  }
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
    auto* config = hcm_filter->mutable_typed_config();
    ASSERT(config->Is<
           envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager>());
    hcm = MessageUtil::anyConvert<
        envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager>(
        *config);
    return true;
  }
  return false;
}

void ConfigHelper::storeHttpConnectionManager(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_, "");
  auto* hcm_config_any =
      getFilterFromListener("envoy.http_connection_manager")->mutable_typed_config();

  hcm_config_any->PackFrom(hcm);
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

void ConfigHelper::setLds(absl::string_view version_info) {
  applyConfigModifiers();

  envoy::api::v2::DiscoveryResponse lds;
  lds.set_version_info(std::string(version_info));
  for (auto& listener : bootstrap_.static_resources().listeners()) {
    ProtobufWkt::Any* resource = lds.add_resources();
    resource->PackFrom(listener);
  }

  const std::string lds_filename = bootstrap().dynamic_resources().lds_config().path();
  std::string file = TestEnvironment::writeStringToFileForTest(
      "new_lds_file", MessageUtil::getJsonStringFromMessage(lds));
  TestUtility::renameFile(file, lds_filename);
}

void ConfigHelper::setOutboundFramesLimits(uint32_t max_all_frames, uint32_t max_control_frames) {
  auto filter = getFilterFromListener("envoy.http_connection_manager");
  if (filter) {
    envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_config;
    loadHttpConnectionManager(hcm_config);
    if (hcm_config.codec_type() ==
        envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::HTTP2) {
      auto* options = hcm_config.mutable_http2_protocol_options();
      options->mutable_max_outbound_frames()->set_value(max_all_frames);
      options->mutable_max_outbound_control_frames()->set_value(max_control_frames);
      storeHttpConnectionManager(hcm_config);
    }
  }
}

CdsHelper::CdsHelper() : cds_path_(TestEnvironment::writeStringToFileForTest("cds.pb_text", "")) {}

void CdsHelper::setCds(const std::vector<envoy::api::v2::Cluster>& clusters) {
  // Write to file the DiscoveryResponse and trigger inotify watch.
  envoy::api::v2::DiscoveryResponse cds_response;
  cds_response.set_version_info(std::to_string(cds_version_++));
  cds_response.set_type_url(Config::TypeUrl::get().Cluster);
  for (const auto& cluster : clusters) {
    cds_response.add_resources()->PackFrom(cluster);
  }
  // Past the initial write, need move semantics to trigger inotify move event that the
  // FilesystemSubscriptionImpl is subscribed to.
  std::string path =
      TestEnvironment::writeStringToFileForTest("cds.update.pb_text", cds_response.DebugString());
  TestUtility::renameFile(path, cds_path_);
}

EdsHelper::EdsHelper() : eds_path_(TestEnvironment::writeStringToFileForTest("eds.pb_text", "")) {
  // cluster.cluster_0.update_success will be incremented on the initial
  // load when Envoy comes up.
  ++update_successes_;
}

void EdsHelper::setEds(
    const std::vector<envoy::api::v2::ClusterLoadAssignment>& cluster_load_assignments) {
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
}

void EdsHelper::setEdsAndWait(
    const std::vector<envoy::api::v2::ClusterLoadAssignment>& cluster_load_assignments,
    IntegrationTestServerStats& server_stats) {
  setEds(cluster_load_assignments);
  // Make sure Envoy has consumed the update now that it is running.
  ++update_successes_;
  server_stats.waitForCounterGe("cluster.cluster_0.update_success", update_successes_);
  RELEASE_ASSERT(
      update_successes_ == server_stats.counter("cluster.cluster_0.update_success")->value(), "");
}

} // namespace Envoy
