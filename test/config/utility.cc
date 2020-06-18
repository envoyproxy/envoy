#include "test/config/utility.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/http/codec.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/assert.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "test/config/integration/certs/client_ecdsacert_hash.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

std::string ConfigHelper::baseConfig() {
  return fmt::format(R"EOF(
admin:
  access_log_path: {}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  lds_config:
    path: {}
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
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
)EOF",
                     TestEnvironment::nullDevicePath(), TestEnvironment::nullDevicePath());
}

std::string ConfigHelper::baseUdpListenerConfig() {
  return fmt::format(R"EOF(
admin:
  access_log_path: {}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
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
)EOF",
                     TestEnvironment::nullDevicePath());
}

std::string ConfigHelper::tcpProxyConfig() {
  return absl::StrCat(baseConfig(), R"EOF(
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp_stats
          cluster: cluster_0
)EOF");
}

std::string ConfigHelper::tlsInspectorFilter() {
  return R"EOF(
name: "envoy.filters.listener.tls_inspector"
typed_config:
)EOF";
}

std::string ConfigHelper::httpProxyConfig() {
  return absl::StrCat(baseConfig(), fmt::format(R"EOF(
    filter_chains:
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          delayed_close_timeout:
            nanos: 100
          http_filters:
            name: envoy.filters.http.router
          codec_type: HTTP1
          access_log:
            name: accesslog
            filter:
              not_health_check_filter:  {{}}
            typed_config:
              "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
              path: {}
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
)EOF",
                                                TestEnvironment::nullDevicePath()));
}

// TODO(danzh): For better compatibility with HTTP integration test framework,
// it's better to combine with HTTP_PROXY_CONFIG, and use config modifiers to
// specify quic specific things.
std::string ConfigHelper::quicHttpProxyConfig() {
  return absl::StrCat(baseUdpListenerConfig(), fmt::format(R"EOF(
    filter_chains:
      transport_socket:
        name: envoy.transport_sockets.quic
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
            name: envoy.filters.http.router
          codec_type: HTTP3
          access_log:
            name: file_access_log
            filter:
              not_health_check_filter:  {{}}
            typed_config:
              "@type": type.googleapis.com/envoy.config.accesslog.v2.FileAccessLog
              path: {}
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
    udp_listener_config:
      udp_listener_name: "quiche_quic_listener"
)EOF",
                                                           TestEnvironment::nullDevicePath()));
}

std::string ConfigHelper::defaultBufferFilter() {
  return R"EOF(
name: buffer
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.buffer.v2.Buffer
    max_request_bytes : 5242880
)EOF";
}

std::string ConfigHelper::smallBufferFilter() {
  return R"EOF(
name: buffer
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.buffer.v2.Buffer
    max_request_bytes : 1024
)EOF";
}

std::string ConfigHelper::defaultHealthCheckFilter() {
  return R"EOF(
name: health_check
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.health_check.v2.HealthCheck
    pass_through_mode: false
)EOF";
}

std::string ConfigHelper::defaultSquashFilter() {
  return R"EOF(
name: squash
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
}

// TODO(fredlas) set_node_on_first_message_only was true; the delta+SotW unification
//               work restores it here.
// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::discoveredClustersBootstrap(const std::string& api_type) {
  return fmt::format(
      R"EOF(
admin:
  access_log_path: {}
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
    load_assignment:
      cluster_name: my_cds_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
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
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
            name: envoy.filters.http.router
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
      TestEnvironment::nullDevicePath(), api_type);
}

// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::adsBootstrap(const std::string& api_type,
                                       envoy::config::core::v3::ApiVersion api_version) {
  return fmt::format(R"EOF(
dynamic_resources:
  lds_config:
    resource_api_version: {1}
    ads: {{}}
  cds_config:
    resource_api_version: {1}
    ads: {{}}
  ads_config:
    transport_api_version: {1}
    api_type: {0}
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout:
      seconds: 5
    type: STATIC
    load_assignment:
      cluster_name: dummy_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {{}}
admin:
  access_log_path: {2}
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     api_type, api_version == envoy::config::core::v3::ApiVersion::V2 ? "V2" : "V3",
                     TestEnvironment::nullDevicePath());
}

// TODO(samflattery): bundle this up with buildCluster
envoy::config::cluster::v3::Cluster
ConfigHelper::buildStaticCluster(const std::string& name, int port, const std::string& address) {
  return TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
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
                                                                                 name, name,
                                                                                 address, port));
}

envoy::config::cluster::v3::Cluster
ConfigHelper::buildCluster(const std::string& name, const std::string& lb_policy,
                           envoy::config::core::v3::ApiVersion api_version) {
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config:
        eds_config:
          resource_api_version: {}
          ads: {{}}
      lb_policy: {}
      http2_protocol_options: {{}}
    )EOF",
                                        name, apiVersionStr(api_version), lb_policy),
                            cluster, shouldBoost(api_version));
  return cluster;
}

envoy::config::endpoint::v3::ClusterLoadAssignment
ConfigHelper::buildClusterLoadAssignment(const std::string& name, const std::string& address,
                                         uint32_t port,
                                         envoy::config::core::v3::ApiVersion api_version) {
  API_NO_BOOST(envoy::config::endpoint::v3::ClusterLoadAssignment) cluster_load_assignment;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      cluster_name: {}
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: {}
    )EOF",
                                        name, address, port),
                            cluster_load_assignment, shouldBoost(api_version));
  return cluster_load_assignment;
}

envoy::config::listener::v3::Listener
ConfigHelper::buildBaseListener(const std::string& name, const std::string& address,
                                const std::string& filter_chains,
                                envoy::config::core::v3::ApiVersion api_version) {
  API_NO_BOOST(envoy::config::listener::v3::Listener) listener;
  TestUtility::loadFromYaml(fmt::format(
                                R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
      {}
    )EOF",
                                name, address, filter_chains),
                            listener, shouldBoost(api_version));
  return listener;
}

envoy::config::listener::v3::Listener
ConfigHelper::buildListener(const std::string& name, const std::string& route_config,
                            const std::string& address, const std::string& stat_prefix,
                            envoy::config::core::v3::ApiVersion api_version) {
  std::string hcm = fmt::format(
      R"EOF(
        filters:
        - name: http
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
            stat_prefix: {}
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source:
                resource_api_version: {}
                ads: {{}}
            http_filters: [{{ name: envoy.filters.http.router }}]
    )EOF",
      stat_prefix, route_config, apiVersionStr(api_version));
  return buildBaseListener(name, address, hcm, api_version);
}

envoy::config::route::v3::RouteConfiguration
ConfigHelper::buildRouteConfig(const std::string& name, const std::string& cluster,
                               envoy::config::core::v3::ApiVersion api_version) {
  API_NO_BOOST(envoy::config::route::v3::RouteConfiguration) route;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                        name, cluster),
                            route, shouldBoost(api_version));
  return route;
}

envoy::config::endpoint::v3::Endpoint ConfigHelper::buildEndpoint(const std::string& address) {
  envoy::config::endpoint::v3::Endpoint endpoint;
  endpoint.mutable_address()->mutable_socket_address()->set_address(address);
  return endpoint;
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
    RELEASE_ASSERT(
        cluster->hidden_envoy_deprecated_hosts().empty(),
        "Hosts should be specified via load_assignment() in the integration test framework.");
    for (int j = 0; j < cluster->load_assignment().endpoints_size(); ++j) {
      auto* locality_lb = cluster->mutable_load_assignment()->mutable_endpoints(j);
      for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
        auto* lb_endpoint = locality_lb->mutable_lb_endpoints(k);
        if (lb_endpoint->endpoint().address().has_socket_address()) {
          lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
              Network::Test::getLoopbackAddressString(version));
        }
      }
    }
  }
}

void ConfigHelper::addClusterFilterMetadata(absl::string_view metadata_yaml,
                                            absl::string_view cluster_name) {
  RELEASE_ASSERT(!finalized_, "");
  ProtobufWkt::Struct cluster_metadata;
  TestUtility::loadFromYaml(std::string(metadata_yaml), cluster_metadata);

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < static_resources->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (cluster->name() != cluster_name) {
      continue;
    }
    for (const auto& kvp : cluster_metadata.fields()) {
      ASSERT_TRUE(kvp.second.kind_case() == ProtobufWkt::Value::KindCase::kStructValue);
      cluster->mutable_metadata()->mutable_filter_metadata()->insert(
          {kvp.first, kvp.second.struct_value()});
    }
    break;
  }
}

void ConfigHelper::setConnectConfig(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm,
    bool terminate_connect) {
  auto* route_config = hcm.mutable_route_config();
  ASSERT_EQ(1, route_config->virtual_hosts_size());
  auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);
  auto* match = route->mutable_match();
  match->Clear();
  match->mutable_connect_matcher();

  if (terminate_connect) {
    auto* upgrade = route->mutable_route()->add_upgrade_configs();
    upgrade->set_upgrade_type("CONNECT");
    upgrade->mutable_connect_config();
  }

  hcm.add_upgrade_configs()->set_upgrade_type("CONNECT");
  hcm.mutable_http2_protocol_options()->set_allow_connect(true);
}

void ConfigHelper::applyConfigModifiers() {
  for (const auto& config_modifier : config_modifiers_) {
    config_modifier(bootstrap_);
  }
  config_modifiers_.clear();
}

void ConfigHelper::addRuntimeOverride(const std::string& key, const std::string& value) {
  if (bootstrap_.mutable_layered_runtime()->layers_size() == 0) {
    auto* static_layer = bootstrap_.mutable_layered_runtime()->add_layers();
    static_layer->set_name("static_layer");
    static_layer->mutable_static_layer();
    auto* admin_layer = bootstrap_.mutable_layered_runtime()->add_layers();
    admin_layer->set_name("admin");
    admin_layer->mutable_admin_layer();
  }
  auto* static_layer =
      bootstrap_.mutable_layered_runtime()->mutable_layers(0)->mutable_static_layer();
  (*static_layer->mutable_fields())[std::string(key)] = ValueUtil::stringValue(std::string(value));
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
        const bool has_tls = filter_chain->has_hidden_envoy_deprecated_tls_context();
        const Protobuf::Message* tls_config = nullptr;
        if (has_tls) {
          tls_config = &filter_chain->hidden_envoy_deprecated_tls_context();
          filter_chain->clear_hidden_envoy_deprecated_tls_context();
        }
        setTapTransportSocket(tap_path.value(), fmt::format("listener_{}_{}", i, j),
                              *filter_chain->mutable_transport_socket(), tls_config);
      }
    }
  }
  for (int i = 0; i < bootstrap_.mutable_static_resources()->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
    if (cluster->type() == envoy::config::cluster::v3::Cluster::EDS) {
      eds_hosts = true;
    } else if (cluster->type() == envoy::config::cluster::v3::Cluster::ORIGINAL_DST) {
      original_dst_cluster = true;
    } else if (cluster->has_cluster_type()) {
      custom_cluster = true;
    } else {
      // Assign ports to statically defined load_assignment hosts.
      RELEASE_ASSERT(
          cluster->hidden_envoy_deprecated_hosts().empty(),
          "Hosts should be specified via load_assignment() in the integration test framework.");
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
      const bool has_tls = cluster->has_hidden_envoy_deprecated_tls_context();
      const Protobuf::Message* tls_config = nullptr;
      if (has_tls) {
        tls_config = &cluster->hidden_envoy_deprecated_tls_context();
        cluster->clear_hidden_envoy_deprecated_tls_context();
      }
      setTapTransportSocket(tap_path.value(), absl::StrCat("cluster_", i),
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
                                         envoy::config::core::v3::TransportSocket& transport_socket,
                                         const Protobuf::Message* tls_config) {
  // Determine inner transport socket.
  envoy::config::core::v3::TransportSocket inner_transport_socket;
  if (!transport_socket.name().empty()) {
    RELEASE_ASSERT(!tls_config, "");
    inner_transport_socket.MergeFrom(transport_socket);
  } else if (tls_config) {
    inner_transport_socket.set_name("envoy.transport_sockets.tls");
    inner_transport_socket.mutable_typed_config()->PackFrom(*tls_config);
  } else {
    inner_transport_socket.set_name("envoy.transport_sockets.raw_buffer");
  }
  // Configure outer tap transport socket.
  transport_socket.set_name("envoy.transport_sockets.tap");
  envoy::extensions::transport_sockets::tap::v3::Tap tap_config;
  tap_config.mutable_common_config()
      ->mutable_static_config()
      ->mutable_match_config()
      ->set_any_match(true);
  auto* output_sink = tap_config.mutable_common_config()
                          ->mutable_static_config()
                          ->mutable_output_config()
                          ->mutable_sinks()
                          ->Add();
  output_sink->set_format(envoy::config::tap::v3::OutputSink::PROTO_TEXT);
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
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
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

  auto filter = getFilterFromListener("http");
  if (filter) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    loadHttpConnectionManager(hcm_config);
    if (hcm_config.codec_type() == envoy::extensions::filters::network::http_connection_manager::
                                       v3::HttpConnectionManager::HTTP2) {
      const uint32_t size = std::max(downstream_buffer_limit,
                                     Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
      auto* options = hcm_config.mutable_http2_protocol_options();
      options->mutable_initial_stream_window_size()->set_value(size);
      storeHttpConnectionManager(hcm_config);
    }
  }
}

void ConfigHelper::setDownstreamHttpIdleTimeout(std::chrono::milliseconds timeout) {
  addConfigModifier(
      [timeout](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_idle_timeout()->MergeFrom(
            ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout.count()));
      });
}

void ConfigHelper::setDownstreamMaxConnectionDuration(std::chrono::milliseconds timeout) {
  addConfigModifier(
      [timeout](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_max_connection_duration()->MergeFrom(
            ProtobufUtil::TimeUtil::MillisecondsToDuration(timeout.count()));
      });
}

void ConfigHelper::setDownstreamMaxStreamDuration(std::chrono::milliseconds timeout) {
  addConfigModifier(
      [timeout](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_common_http_protocol_options()->mutable_max_stream_duration()->MergeFrom(
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

envoy::config::route::v3::VirtualHost
ConfigHelper::createVirtualHost(const char* domain, const char* prefix, const char* cluster) {
  envoy::config::route::v3::VirtualHost virtual_host;
  virtual_host.set_name(domain);
  virtual_host.add_domains(domain);
  virtual_host.add_routes()->mutable_match()->set_prefix(prefix);
  auto* route = virtual_host.mutable_routes(0)->mutable_route();
  route->set_cluster(cluster);
  return virtual_host;
}

void ConfigHelper::addVirtualHost(const envoy::config::route::v3::VirtualHost& vhost) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  loadHttpConnectionManager(hcm_config);
  auto route_config = hcm_config.mutable_route_config();
  auto* virtual_host = route_config->add_virtual_hosts();
  virtual_host->CopyFrom(vhost);
  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::addFilter(const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  loadHttpConnectionManager(hcm_config);

  auto* filter_list_back = hcm_config.add_http_filters();
  TestUtility::loadFromYaml(config, *filter_list_back);

  // Now move it to the front.
  for (int i = hcm_config.http_filters_size() - 1; i > 0; --i) {
    hcm_config.mutable_http_filters()->SwapElements(i, i - 1);
  }
  storeHttpConnectionManager(hcm_config);
}

void ConfigHelper::setClientCodec(envoy::extensions::filters::network::http_connection_manager::v3::
                                      HttpConnectionManager::CodecType type) {
  RELEASE_ASSERT(!finalized_, "");
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  if (loadHttpConnectionManager(hcm_config)) {
    hcm_config.set_codec_type(type);
    storeHttpConnectionManager(hcm_config);
  }
}

void ConfigHelper::addSslConfig(const ServerSslOptions& options) {
  RELEASE_ASSERT(!finalized_, "");

  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  initializeTls(options, *tls_context.mutable_common_tls_context());
  filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
}

bool ConfigHelper::setAccessLog(const std::string& filename, absl::string_view format) {
  if (getFilterFromListener("http") == nullptr) {
    return false;
  }
  // Replace null device with a real path for the file access log.
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  loadHttpConnectionManager(hcm_config);
  envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
  if (!format.empty()) {
    access_log_config.mutable_log_format()->set_text_format(absl::StrCat(format, "\n"));
  }
  access_log_config.set_path(filename);
  hcm_config.mutable_access_log(0)->mutable_typed_config()->PackFrom(access_log_config);
  storeHttpConnectionManager(hcm_config);
  return true;
}

bool ConfigHelper::setListenerAccessLog(const std::string& filename, absl::string_view format) {
  RELEASE_ASSERT(!finalized_, "");
  if (bootstrap_.mutable_static_resources()->listeners_size() == 0) {
    return false;
  }
  envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
  if (!format.empty()) {
    access_log_config.mutable_log_format()->set_text_format(std::string(format));
  }
  access_log_config.set_path(filename);
  bootstrap_.mutable_static_resources()
      ->mutable_listeners(0)
      ->add_access_log()
      ->mutable_typed_config()
      ->PackFrom(access_log_config);
  return true;
}

void ConfigHelper::initializeTls(
    const ServerSslOptions& options,
    envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
  common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
  common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

  auto* validation_context = common_tls_context.mutable_validation_context();
  validation_context->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
  validation_context->add_verify_certificate_hash(
      options.expect_client_ecdsa_cert_ ? TEST_CLIENT_ECDSA_CERT_HASH : TEST_CLIENT_CERT_HASH);

  // We'll negotiate up to TLSv1.3 for the tests that care, but it really
  // depends on what the client sets.
  common_tls_context.mutable_tls_params()->set_tls_maximum_protocol_version(
      options.tlsv1_3_ ? envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3
                       : envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
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

envoy::config::listener::v3::Filter* ConfigHelper::getFilterFromListener(const std::string& name) {
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

void ConfigHelper::addNetworkFilter(const std::string& filter_yaml) {
  RELEASE_ASSERT(!finalized_, "");
  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
  auto* filter_list_back = filter_chain->add_filters();
  TestUtility::loadFromYaml(filter_yaml, *filter_list_back);

  // Now move it to the front.
  for (int i = filter_chain->filters_size() - 1; i > 0; --i) {
    filter_chain->mutable_filters()->SwapElements(i, i - 1);
  }
}

void ConfigHelper::addListenerFilter(const std::string& filter_yaml) {
  RELEASE_ASSERT(!finalized_, "");
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  auto* filter_list_back = listener->add_listener_filters();
  TestUtility::loadFromYaml(filter_yaml, *filter_list_back);

  // Now move it to the front.
  for (int i = listener->listener_filters_size() - 1; i > 0; --i) {
    listener->mutable_listener_filters()->SwapElements(i, i - 1);
  }
}

bool ConfigHelper::loadHttpConnectionManager(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  RELEASE_ASSERT(!finalized_, "");
  auto* hcm_filter = getFilterFromListener("http");
  if (hcm_filter) {
    auto* config = hcm_filter->mutable_typed_config();
    hcm = MessageUtil::anyConvert<
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
        *config);
    return true;
  }
  return false;
}

void ConfigHelper::storeHttpConnectionManager(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        hcm) {
  RELEASE_ASSERT(!finalized_, "");
  auto* hcm_config_any = getFilterFromListener("http")->mutable_typed_config();

  hcm_config_any->PackFrom(hcm);
}

void ConfigHelper::addConfigModifier(ConfigModifierFunction function) {
  RELEASE_ASSERT(!finalized_, "");
  config_modifiers_.push_back(std::move(function));
}

void ConfigHelper::addConfigModifier(HttpModifierFunction function) {
  addConfigModifier([function, this](envoy::config::bootstrap::v3::Bootstrap&) -> void {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    loadHttpConnectionManager(hcm_config);
    function(hcm_config);
    storeHttpConnectionManager(hcm_config);
  });
}

void ConfigHelper::setLds(absl::string_view version_info) {
  applyConfigModifiers();

  envoy::service::discovery::v3::DiscoveryResponse lds;
  lds.set_version_info(std::string(version_info));
  for (auto& listener : bootstrap_.static_resources().listeners()) {
    ProtobufWkt::Any* resource = lds.add_resources();
    resource->PackFrom(listener);
  }

  const std::string lds_filename = bootstrap().dynamic_resources().lds_config().path();
  std::string file = TestEnvironment::writeStringToFileForTest(
      "new_lds_file", MessageUtil::getJsonStringFromMessage(lds));
  TestEnvironment::renameFile(file, lds_filename);
}

void ConfigHelper::setOutboundFramesLimits(uint32_t max_all_frames, uint32_t max_control_frames) {
  auto filter = getFilterFromListener("http");
  if (filter) {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    loadHttpConnectionManager(hcm_config);
    if (hcm_config.codec_type() == envoy::extensions::filters::network::http_connection_manager::
                                       v3::HttpConnectionManager::HTTP2) {
      auto* options = hcm_config.mutable_http2_protocol_options();
      options->mutable_max_outbound_frames()->set_value(max_all_frames);
      options->mutable_max_outbound_control_frames()->set_value(max_control_frames);
      storeHttpConnectionManager(hcm_config);
    }
  }
}

void ConfigHelper::setLocalReply(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config) {
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  loadHttpConnectionManager(hcm_config);
  hcm_config.mutable_local_reply_config()->MergeFrom(config);
  storeHttpConnectionManager(hcm_config);
}

CdsHelper::CdsHelper() : cds_path_(TestEnvironment::writeStringToFileForTest("cds.pb_text", "")) {}

void CdsHelper::setCds(const std::vector<envoy::config::cluster::v3::Cluster>& clusters) {
  // Write to file the DiscoveryResponse and trigger inotify watch.
  envoy::service::discovery::v3::DiscoveryResponse cds_response;
  cds_response.set_version_info(std::to_string(cds_version_++));
  cds_response.set_type_url(Config::TypeUrl::get().Cluster);
  for (const auto& cluster : clusters) {
    cds_response.add_resources()->PackFrom(cluster);
  }
  // Past the initial write, need move semantics to trigger inotify move event that the
  // FilesystemSubscriptionImpl is subscribed to.
  std::string path =
      TestEnvironment::writeStringToFileForTest("cds.update.pb_text", cds_response.DebugString());
  TestEnvironment::renameFile(path, cds_path_);
}

EdsHelper::EdsHelper() : eds_path_(TestEnvironment::writeStringToFileForTest("eds.pb_text", "")) {
  // cluster.cluster_0.update_success will be incremented on the initial
  // load when Envoy comes up.
  ++update_successes_;
}

void EdsHelper::setEds(const std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment>&
                           cluster_load_assignments) {
  // Write to file the DiscoveryResponse and trigger inotify watch.
  envoy::service::discovery::v3::DiscoveryResponse eds_response;
  eds_response.set_version_info(std::to_string(eds_version_++));
  eds_response.set_type_url(Config::TypeUrl::get().ClusterLoadAssignment);
  for (const auto& cluster_load_assignment : cluster_load_assignments) {
    eds_response.add_resources()->PackFrom(cluster_load_assignment);
  }
  // Past the initial write, need move semantics to trigger inotify move event that the
  // FilesystemSubscriptionImpl is subscribed to.
  std::string path =
      TestEnvironment::writeStringToFileForTest("eds.update.pb_text", eds_response.DebugString());
  TestEnvironment::renameFile(path, eds_path_);
}

void EdsHelper::setEdsAndWait(
    const std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment>& cluster_load_assignments,
    IntegrationTestServerStats& server_stats) {
  // Make sure the last version has been accepted before setting a new one.
  server_stats.waitForCounterGe("cluster.cluster_0.update_success", update_successes_);
  setEds(cluster_load_assignments);
  // Make sure Envoy has consumed the update now that it is running.
  ++update_successes_;
  server_stats.waitForCounterGe("cluster.cluster_0.update_success", update_successes_);
  RELEASE_ASSERT(
      update_successes_ == server_stats.counter("cluster.cluster_0.update_success")->value(), "");
}

} // namespace Envoy
