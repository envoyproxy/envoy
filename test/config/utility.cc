#include "test/config/utility.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common.pb.h"
#include "envoy/http/codec.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "test/config/integration/certs/client2cert_hash.h"
#include "test/config/integration/certs/client_ecdsacert_hash.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {

std::string ConfigHelper::baseConfigNoListeners() {
  return fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  lds_config:
    resource_api_version: V3
    path_config_source:
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
)EOF",
                     Platform::null_device_path, Platform::null_device_path);
}

std::string ConfigHelper::baseConfig(bool multiple_addresses) {
  if (multiple_addresses) {
    return absl::StrCat(baseConfigNoListeners(), R"EOF(
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    additional_addresses:
    - address:
        socket_address:
          address: 127.0.0.1
          port_value: 0
)EOF");
  }
  return absl::StrCat(baseConfigNoListeners(), R"EOF(
  listeners:
  - name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
)EOF");
}

std::string ConfigHelper::baseUdpListenerConfig(std::string listen_address,
                                                bool multiple_addresses) {
  std::string config = fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
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
)EOF",
                                   Platform::null_device_path);

  if (multiple_addresses) {
    return absl::StrCat(config, fmt::format(R"EOF(
  listeners:
    name: listener_0
    address:
      socket_address:
        address: {}
        port_value: 0
        protocol: udp
    additional_addresses:
    - address:
        socket_address:
          address: {}
          port_value: 0
          protocol: udp
)EOF",
                                            listen_address, listen_address));
  }
  return absl::StrCat(config, fmt::format(R"EOF(
  listeners:
    name: listener_0
    address:
      socket_address:
        address: {}
        port_value: 0
        protocol: udp
)EOF",
                                          listen_address));
}

std::string ConfigHelper::tcpProxyConfig() {
  return absl::StrCat(baseConfig(), R"EOF(
    filter_chains:
      filters:
        name: tcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcpproxy_stats
          cluster: cluster_0
)EOF");
}

std::string ConfigHelper::startTlsConfig() {
  return absl::StrCat(
      tcpProxyConfig(),
      fmt::format(R"EOF(
      transport_socket:
        name: "starttls"
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.StartTlsConfig
          cleartext_socket_config:
          tls_socket_config:
            common_tls_context:
              tls_certificates:
                certificate_chain:
                  filename: {}
                private_key:
                  filename: {}
)EOF",
                  TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"),
                  TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem")));
}

std::string ConfigHelper::testInspectorFilter() {
  return R"EOF(
name: "envoy.filters.listener.test"
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF";
}

std::string ConfigHelper::tlsInspectorFilter(bool enable_ja3_fingerprinting) {
  if (!enable_ja3_fingerprinting) {
    return R"EOF(
name: "envoy.filters.listener.tls_inspector"
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
)EOF";
  }

  return R"EOF(
name: "envoy.filters.listener.tls_inspector"
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
  enable_ja3_fingerprinting: true
)EOF";
}

std::string ConfigHelper::httpProxyConfig(bool downstream_use_quic, bool multiple_addresses) {
  if (downstream_use_quic) {
    return quicHttpProxyConfig(multiple_addresses);
  }
  return absl::StrCat(baseConfig(multiple_addresses), fmt::format(R"EOF(
    filter_chains:
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          delayed_close_timeout:
            nanos: 10000000
          http_filters:
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
          codec_type: HTTP1
          access_log:
            name: accesslog
            filter:
              not_health_check_filter:  {{}}
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
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
                                                                  Platform::null_device_path));
}

// TODO(danzh): For better compatibility with HTTP integration test framework,
// it's better to combine with HTTP_PROXY_CONFIG, and use config modifiers to
// specify quic specific things.
std::string ConfigHelper::quicHttpProxyConfig(bool multiple_addresses) {
  return absl::StrCat(baseUdpListenerConfig("127.0.0.1", multiple_addresses),
                      fmt::format(R"EOF(
    filter_chains:
      transport_socket:
        name: envoy.transport_sockets.quic
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicDownstreamTransport
      filters:
        name: http
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
          codec_type: HTTP3
          access_log:
            name: file_access_log
            filter:
              not_health_check_filter:  {{}}
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
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
      quic_options: {{}}
)EOF",
                                  Platform::null_device_path));
}

std::string ConfigHelper::defaultBufferFilter() {
  return R"EOF(
name: buffer
typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
    max_request_bytes : 5242880
)EOF";
}

std::string ConfigHelper::smallBufferFilter() {
  return R"EOF(
name: buffer
typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
    max_request_bytes : 1024
)EOF";
}

std::string ConfigHelper::defaultHealthCheckFilter() {
  return R"EOF(
name: health_check
typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
    pass_through_mode: false
)EOF";
}

std::string ConfigHelper::defaultSquashFilter() {
  return R"EOF(
name: squash
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.squash.v3.Squash
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

// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::discoveredClustersBootstrap(const std::string& api_type) {
  return fmt::format(
      R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
dynamic_resources:
  cds_config:
    resource_api_version: V3
    api_config_source:
      api_type: {}
      transport_api_version: V3
      grpc_services:
        envoy_grpc:
          cluster_name: my_cds_cluster
      set_node_on_first_message_only: true
static_resources:
  clusters:
  - name: my_cds_cluster
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
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
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: config_test
          http_filters:
          - name: envoy.filters.http.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
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
      Platform::null_device_path, api_type);
}

// TODO(#6327) cleaner approach to testing with static config.
std::string ConfigHelper::adsBootstrap(const std::string& api_type) {
  // We use this to allow tests to default to having a single API version but override and make
  // the transport/resource API version distinction when needed.
  return fmt::format(R"EOF(
dynamic_resources:
  lds_config:
    resource_api_version: V3
    ads: {{}}
  cds_config:
    resource_api_version: V3
    ads: {{}}
  ads_config:
    transport_api_version: V3
    api_type: {0}
    set_node_on_first_message_only: true
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
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {{}}
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{1}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF",
                     api_type, Platform::null_device_path);
}

// TODO(samflattery): bundle this up with buildCluster
envoy::config::cluster::v3::Cluster ConfigHelper::buildStaticCluster(const std::string& name,
                                                                     int port,
                                                                     const std::string& address,
                                                                     const std::string& lb_policy) {
  return TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
      fmt::format(R"EOF(
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
              health_check_config:
                address:
                  socket_address:
                    address: {}
                    port_value: {}
      lb_policy: {}
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options: {{}}
    )EOF",
                  name, name, address, port, address, port, lb_policy));
}

envoy::config::cluster::v3::Cluster ConfigHelper::buildH1ClusterWithHighCircuitBreakersLimits(
    const std::string& name, int port, const std::string& address, const std::string& lb_policy) {
  return TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(
      fmt::format(R"EOF(
      name: {}
      connect_timeout: 50s
      type: STATIC
      circuit_breakers:
        thresholds:
        - priority: DEFAULT
          max_connections: 10000
          max_pending_requests: 10000
          max_requests: 10000
          max_retries: 10000
      load_assignment:
        cluster_name: {}
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: {}
                  port_value: {}
      lb_policy: {}
    )EOF",
                  name, name, address, port, lb_policy));
}

envoy::config::cluster::v3::Cluster ConfigHelper::buildCluster(const std::string& name,
                                                               const std::string& lb_policy) {
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config:
        eds_config:
          resource_api_version: V3
          ads: {{}}
      lb_policy: {}
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options: {{}}
    )EOF",
                                        name, lb_policy),
                            cluster);
  return cluster;
}

envoy::config::cluster::v3::Cluster ConfigHelper::buildTlsCluster(const std::string& name,
                                                                  const std::string& lb_policy) {
  API_NO_BOOST(envoy::config::cluster::v3::Cluster) cluster;
  TestUtility::loadFromYaml(
      fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config:
        eds_config:
          resource_api_version: V3
          ads: {{}}
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
          common_tls_context:
            validation_context:
              trusted_ca:
                filename: {}
      lb_policy: {}
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options: {{}}
    )EOF",
                  name,
                  TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"),
                  lb_policy),
      cluster);
  return cluster;
}

envoy::config::endpoint::v3::ClusterLoadAssignment
ConfigHelper::buildClusterLoadAssignment(const std::string& name, const std::string& address,
                                         uint32_t port) {
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
                            cluster_load_assignment);
  return cluster_load_assignment;
}

envoy::config::endpoint::v3::ClusterLoadAssignment
ConfigHelper::buildClusterLoadAssignmentWithLeds(const std::string& name,
                                                 const std::string& leds_collection_name) {
  API_NO_BOOST(envoy::config::endpoint::v3::ClusterLoadAssignment) cluster_load_assignment;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      cluster_name: {}
      endpoints:
        leds_cluster_locality_config:
          leds_config:
            resource_api_version: V3
            ads: {{}}
          leds_collection_name: {}
    )EOF",
                                        name, leds_collection_name),
                            cluster_load_assignment);
  return cluster_load_assignment;
}

envoy::config::endpoint::v3::LbEndpoint ConfigHelper::buildLbEndpoint(const std::string& address,
                                                                      uint32_t port) {
  API_NO_BOOST(envoy::config::endpoint::v3::LbEndpoint) lb_endpoint;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      endpoint:
        address:
          socket_address:
            address: {}
            port_value: {}
    )EOF",
                                        address, port),
                            lb_endpoint);
  return lb_endpoint;
}

envoy::config::listener::v3::Listener
ConfigHelper::buildBaseListener(const std::string& name, const std::string& address,
                                const std::string& filter_chains) {
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
                            listener);
  return listener;
}

envoy::config::listener::v3::Listener ConfigHelper::buildListener(const std::string& name,
                                                                  const std::string& route_config,
                                                                  const std::string& address,
                                                                  const std::string& stat_prefix) {
  std::string hcm = fmt::format(
      R"EOF(
        filters:
        - name: http
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: {}
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source:
                resource_api_version: V3
                ads: {{}}
            http_filters:
            - name: envoy.filters.http.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    )EOF",
      stat_prefix, route_config);
  return buildBaseListener(name, address, hcm);
}

envoy::config::route::v3::RouteConfiguration
ConfigHelper::buildRouteConfig(const std::string& name, const std::string& cluster) {
  API_NO_BOOST(envoy::config::route::v3::RouteConfiguration) route;
  TestUtility::loadFromYaml(fmt::format(R"EOF(
      name: "{}"
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: "{}" }}
    )EOF",
                                        name, cluster),
                            route);
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
    if (listener->mutable_address()->has_envoy_internal_address()) {
      ENVOY_LOG_MISC(
          debug, "Listener {} has internal address {}. Will not reset to loop back socket address.",
          i, listener->mutable_address()->envoy_internal_address().server_listener_name());
      continue;
    }
    if (listener->mutable_address()->has_pipe()) {
      ENVOY_LOG_MISC(debug,
                     "Listener {} has pipe address {}. Will not reset to loop back socket address.",
                     i, listener->mutable_address()->pipe().path());
      continue;
    }
    auto* listener_socket_addr = listener->mutable_address()->mutable_socket_address();
    if (listener_socket_addr->address() == "0.0.0.0" || listener_socket_addr->address() == "::") {
      listener_socket_addr->set_address(Network::Test::getAnyAddressString(version));
    } else {
      listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
    }

    for (int i = 0; i < listener->additional_addresses_size(); i++) {
      auto* listener_socket_addr =
          listener->mutable_additional_addresses(i)->mutable_address()->mutable_socket_address();
      if (listener_socket_addr->address() == "0.0.0.0" || listener_socket_addr->address() == "::") {
        listener_socket_addr->set_address(Network::Test::getAnyAddressString(version));
      } else {
        listener_socket_addr->set_address(Network::Test::getLoopbackAddressString(version));
      }
    }
  }

  for (int i = 0; i < static_resources->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);
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

  // Ensure we have a basic admin-capable runtime layer.
  if (bootstrap_.mutable_layered_runtime()->layers_size() == 0) {
    auto* static_layer = bootstrap_.mutable_layered_runtime()->add_layers();
    static_layer->set_name("static_layer");
    static_layer->mutable_static_layer();
    auto* admin_layer = bootstrap_.mutable_layered_runtime()->add_layers();
    admin_layer->set_name("admin");
    admin_layer->mutable_admin_layer();
  }
}

void ConfigHelper::addListenerTypedMetadata(absl::string_view key, ProtobufWkt::Any& packed_value) {
  RELEASE_ASSERT(!finalized_, "");
  auto* static_resources = bootstrap_.mutable_static_resources();
  ASSERT_TRUE(static_resources->listeners_size() > 0);
  auto* listener = static_resources->mutable_listeners(0);
  auto* map = listener->mutable_metadata()->mutable_typed_filter_metadata();
  (*map)[std::string(key)] = packed_value;
};

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
    bool terminate_connect, bool allow_post, bool http3) {
  auto* route_config = hcm.mutable_route_config();
  ASSERT_EQ(1, route_config->virtual_hosts_size());
  auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);
  auto* match = route->mutable_match();
  match->Clear();

  if (allow_post) {
    match->set_prefix("/");

    auto* header = match->add_headers();
    header->set_name(":method");
    header->mutable_string_match()->set_exact("POST");
  } else {
    match->mutable_connect_matcher();
  }

  if (terminate_connect) {
    auto* upgrade = route->mutable_route()->add_upgrade_configs();
    upgrade->set_upgrade_type("CONNECT");
    auto* config = upgrade->mutable_connect_config();
    if (allow_post) {
      config->set_allow_post(true);
    }
  }

  hcm.add_upgrade_configs()->set_upgrade_type("CONNECT");
  hcm.mutable_http2_protocol_options()->set_allow_connect(true);
  if (http3) {
    hcm.mutable_http3_protocol_options()->set_allow_extended_connect(true);
  }
}

void ConfigHelper::applyConfigModifiers() {
  for (const auto& config_modifier : config_modifiers_) {
    config_modifier(bootstrap_);
  }
  config_modifiers_.clear();
}

void ConfigHelper::configureUpstreamTls(
    bool use_alpn, bool http3,
    absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>
        alternate_protocol_cache_config) {
  addConfigModifier([use_alpn, http3, alternate_protocol_cache_config](
                        envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    ConfigHelper::setProtocolOptions(*cluster, protocol_options);

    if (use_alpn) {
      ConfigHelper::HttpProtocolOptions new_protocol_options;

      HttpProtocolOptions old_protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      protocol_options.MergeFrom(old_protocol_options);

      new_protocol_options = old_protocol_options;
      new_protocol_options.clear_explicit_http_config();
      new_protocol_options.mutable_auto_config();
      if (old_protocol_options.explicit_http_config().has_http_protocol_options()) {
        new_protocol_options.mutable_auto_config()->mutable_http_protocol_options()->MergeFrom(
            old_protocol_options.explicit_http_config().http_protocol_options());
      } else if (old_protocol_options.explicit_http_config().has_http2_protocol_options()) {
        new_protocol_options.mutable_auto_config()->mutable_http2_protocol_options()->MergeFrom(
            old_protocol_options.explicit_http_config().http2_protocol_options());
      }
      if (http3 || old_protocol_options.explicit_http_config().has_http3_protocol_options()) {
        new_protocol_options.mutable_auto_config()->mutable_http3_protocol_options()->MergeFrom(
            old_protocol_options.explicit_http_config().http3_protocol_options());
      }
      if (alternate_protocol_cache_config.has_value()) {
        new_protocol_options.mutable_auto_config()
            ->mutable_alternate_protocols_cache_options()
            ->set_name("default_alternate_protocols_cache");
        new_protocol_options.mutable_auto_config()
            ->mutable_alternate_protocols_cache_options()
            ->CopyFrom(alternate_protocol_cache_config.value());
      }
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(new_protocol_options);
    }
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    auto* validation_context =
        tls_context.mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    // The test certs are for *.lyft.com, so make sure SNI matches.
    tls_context.set_sni("foo.lyft.com");
    if (http3) {
      envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport quic_context;
      quic_context.mutable_upstream_tls_context()->CopyFrom(tls_context);
      cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.quic");
      cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(quic_context);
    } else {
      cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    }
  });
}

void ConfigHelper::addRuntimeOverride(absl::string_view key, absl::string_view value) {
  auto* static_layer =
      bootstrap_.mutable_layered_runtime()->mutable_layers(0)->mutable_static_layer();
  (*static_layer->mutable_fields())[std::string(key)] = ValueUtil::stringValue(std::string(value));
}

void ConfigHelper::setProtocolOptions(envoy::config::cluster::v3::Cluster& cluster,
                                      HttpProtocolOptions& protocol_options) {
  if (cluster.typed_extension_protocol_options().contains(
          "envoy.extensions.upstreams.http.v3.HttpProtocolOptions")) {
    HttpProtocolOptions old_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
        (*cluster.mutable_typed_extension_protocol_options())
            ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
    bool auto_config = old_options.has_auto_config();
    old_options.MergeFrom(protocol_options);
    protocol_options.CopyFrom(old_options);
    if (auto_config) {
      protocol_options.mutable_auto_config();
    }
  }
  (*cluster.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(protocol_options);
}

void ConfigHelper::setHttp2(envoy::config::cluster::v3::Cluster& cluster) {
  HttpProtocolOptions protocol_options;
  protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
  setProtocolOptions(cluster, protocol_options);
}

void ConfigHelper::finalize(const std::vector<uint32_t>& ports) {
  RELEASE_ASSERT(!finalized_, "");

  applyConfigModifiers();

  setPorts(ports);

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

void ConfigHelper::setPorts(const std::vector<uint32_t>& ports, bool override_port_zero) {
  uint32_t port_idx = 0;
  bool eds_hosts = false;
  bool custom_cluster = false;
  bool original_dst_cluster = false;
  auto* static_resources = bootstrap_.mutable_static_resources();
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
      for (int j = 0; j < cluster->load_assignment().endpoints_size(); ++j) {
        auto locality_lb = cluster->mutable_load_assignment()->mutable_endpoints(j);
        for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
          auto lb_endpoint = locality_lb->mutable_lb_endpoints(k);
          if (lb_endpoint->endpoint().address().has_socket_address()) {
            if (lb_endpoint->endpoint().address().socket_address().port_value() == 0 ||
                override_port_zero) {
              RELEASE_ASSERT(ports.size() > port_idx, "");
              lb_endpoint->mutable_endpoint()
                  ->mutable_address()
                  ->mutable_socket_address()
                  ->set_port_value(ports[port_idx++]);
            } else {
              ENVOY_LOG_MISC(debug, "Not overriding preset port",
                             lb_endpoint->endpoint().address().socket_address().port_value());
            }
          }
        }
      }
    }
  }
  ASSERT(skip_port_usage_validation_ || port_idx == ports.size() || eds_hosts ||
         original_dst_cluster || custom_cluster || bootstrap_.dynamic_resources().has_cds_config());
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
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  listener->mutable_per_connection_buffer_limit_bytes()->set_value(downstream_buffer_limit);
  const uint32_t stream_buffer_size = std::max(
      downstream_buffer_limit, Http2::Utility::OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE);
  if (Network::Utility::protobufAddressSocketType(listener->address()) ==
          Network::Socket::Type::Datagram &&
      listener->udp_listener_config().has_quic_options()) {
    // QUIC stream's flow control window is configured in listener config.
    listener->mutable_udp_listener_config()
        ->mutable_quic_options()
        ->mutable_quic_protocol_options()
        ->mutable_initial_stream_window_size()
        // The same as kStreamReceiveWindowLimit in QUICHE which only supports stream flow control
        // window no larger than 16MB.
        ->set_value(std::min(16u * 1024 * 1024, stream_buffer_size));
  }

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
      auto* options = hcm_config.mutable_http2_protocol_options();
      options->mutable_initial_stream_window_size()->set_value(stream_buffer_size);
      storeHttpConnectionManager(hcm_config);
    }
  }
}

void ConfigHelper::setListenerSendBufLimits(uint32_t limit) {
  RELEASE_ASSERT(!finalized_, "");
  RELEASE_ASSERT(bootstrap_.mutable_static_resources()->listeners_size() == 1, "");
  auto* listener = bootstrap_.mutable_static_resources()->mutable_listeners(0);
  auto* options = listener->add_socket_options();
  options->set_description("SO_SNDBUF");
  options->set_level(SOL_SOCKET);
  options->set_int_value(limit);
  options->set_name(SO_SNDBUF);
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

void ConfigHelper::disableDelayClose() {
  addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_nanos(0); });
}

void ConfigHelper::setDownstreamMaxRequestsPerConnection(uint64_t max_requests_per_connection) {
  addConfigModifier(
      [max_requests_per_connection](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_common_http_protocol_options()
            ->mutable_max_requests_per_connection()
            ->set_value(max_requests_per_connection);
      });
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

void ConfigHelper::addFilter(const std::string& config) { prependFilter(config); }

void ConfigHelper::prependFilter(const std::string& config, bool downstream) {
  RELEASE_ASSERT(!finalized_, "");
  if (downstream) {
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
    return;
  }

  auto* static_resources = bootstrap_.mutable_static_resources();
  for (int i = 0; i < static_resources->clusters_size(); ++i) {
    auto* cluster = static_resources->mutable_clusters(i);

    HttpProtocolOptions old_protocol_options;
    if (cluster->typed_extension_protocol_options().contains(
            "envoy.extensions.upstreams.http.v3.HttpProtocolOptions")) {
      old_protocol_options = MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
          (*cluster->mutable_typed_extension_protocol_options())
              ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
    }
    if (old_protocol_options.http_filters().empty()) {
      old_protocol_options.add_http_filters()->set_name("envoy.filters.http.upstream_codec");
    }
    auto* filter_list_back = old_protocol_options.add_http_filters();
    TestUtility::loadFromYaml(config, *filter_list_back);
    for (int i = old_protocol_options.http_filters_size() - 1; i > 0; --i) {
      old_protocol_options.mutable_http_filters()->SwapElements(i, i - 1);
    }
    (*cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(old_protocol_options);
  }
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

void ConfigHelper::configDownstreamTransportSocketWithTls(
    envoy::config::bootstrap::v3::Bootstrap& bootstrap,
    std::function<void(envoy::extensions::transport_sockets::tls::v3::CommonTlsContext&)>
        configure_tls_context,
    bool enable_quic_early_data) {
  for (auto& listener : *bootstrap.mutable_static_resources()->mutable_listeners()) {
    ASSERT(listener.filter_chains_size() > 0);
    auto* filter_chain = listener.mutable_filter_chains(0);
    auto* transport_socket = filter_chain->mutable_transport_socket();
    if (listener.has_udp_listener_config() && listener.udp_listener_config().has_quic_options()) {
      transport_socket->set_name("envoy.transport_sockets.quic");
      envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport
          quic_transport_socket_config;
      configure_tls_context(*quic_transport_socket_config.mutable_downstream_tls_context()
                                 ->mutable_common_tls_context());
      quic_transport_socket_config.mutable_enable_early_data()->set_value(enable_quic_early_data);
      transport_socket->mutable_typed_config()->PackFrom(quic_transport_socket_config);
    } else if (!listener.has_udp_listener_config()) {
      transport_socket->set_name("envoy.transport_sockets.tls");
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      configure_tls_context(*tls_context.mutable_common_tls_context());
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    }
  }
}

void ConfigHelper::addSslConfig(const ServerSslOptions& options) {
  RELEASE_ASSERT(!finalized_, "");

  auto* filter_chain =
      bootstrap_.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  initializeTls(options, *tls_context.mutable_common_tls_context());
  if (options.ocsp_staple_required_) {
    tls_context.set_ocsp_staple_policy(
        envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::MUST_STAPLE);
  }
  filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
}

void ConfigHelper::addQuicDownstreamTransportSocketConfig(bool enable_early_data) {
  for (auto& listener : *bootstrap_.mutable_static_resources()->mutable_listeners()) {
    if (listener.udp_listener_config().has_quic_options()) {
      // Disable SO_REUSEPORT, because it undesirably allows parallel test jobs to use the same
      // port.
      listener.mutable_enable_reuse_port()->set_value(false);
    }
  }
  configDownstreamTransportSocketWithTls(
      bootstrap_,
      [](envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
        initializeTls(ServerSslOptions().setRsaCert(true).setTlsV13(true), common_tls_context);
      },
      enable_early_data);
}

bool ConfigHelper::setAccessLog(
    const std::string& filename, absl::string_view format,
    std::vector<envoy::config::core::v3::TypedExtensionConfig> formatters) {
  if (getFilterFromListener("http") == nullptr) {
    return false;
  }
  // Replace null device with a real path for the file access log.
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
      hcm_config;
  loadHttpConnectionManager(hcm_config);
  envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
  if (!format.empty()) {
    auto* log_format = access_log_config.mutable_log_format();
    log_format->mutable_text_format_source()->set_inline_string(absl::StrCat(format, "\n"));
    if (!formatters.empty()) {
      for (const auto& formatter : formatters) {
        auto* added_formatter = log_format->add_formatters();
        added_formatter->CopyFrom(formatter);
      }
    }
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
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        std::string(format));
  }
  access_log_config.set_path(filename);
  bootstrap_.mutable_static_resources()
      ->mutable_listeners(0)
      ->add_access_log()
      ->mutable_typed_config()
      ->PackFrom(access_log_config);
  return true;
}

void ConfigHelper::initializeTlsKeyLog(
    envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& tls_context,
    const ServerSslOptions& options) {
  if (options.keylog_path_.empty()) {
    return;
  }
  auto tls_keylog_path = tls_context.mutable_key_log()->mutable_path();
  *tls_keylog_path = options.keylog_path_;

  if (options.keylog_local_filter_) {
    auto tls_keylog_local = tls_context.mutable_key_log()->mutable_local_address_range();
    auto new_element_local = tls_keylog_local->Add();
    if (options.keylog_local_negative_) {
      if (options.ip_version_ == Network::Address::IpVersion::v6) {
        new_element_local->set_address_prefix("1::2");
        new_element_local->mutable_prefix_len()->set_value(128);
      } else {
        new_element_local->set_address_prefix("127.0.0.2");
        new_element_local->mutable_prefix_len()->set_value(32);
      }
    } else {
      new_element_local->set_address_prefix(
          Network::Test::getLoopbackAddressString(options.ip_version_));
      if (options.keylog_multiple_ips_) {
        auto more_local = tls_keylog_local->Add();
        if (options.ip_version_ == Network::Address::IpVersion::v6) {
          more_local->set_address_prefix("1::2");
          more_local->mutable_prefix_len()->set_value(128);
        } else {
          more_local->set_address_prefix("127.0.0.2");
          more_local->mutable_prefix_len()->set_value(32);
        }
      }
    }
  }

  if (options.keylog_remote_filter_) {
    auto tls_keylog_remote = tls_context.mutable_key_log()->mutable_remote_address_range();
    auto new_element_remote = tls_keylog_remote->Add();
    if (options.keylog_remote_negative_) {
      if (options.ip_version_ == Network::Address::IpVersion::v6) {
        new_element_remote->set_address_prefix("1::2");
        new_element_remote->mutable_prefix_len()->set_value(128);
      } else {
        new_element_remote->set_address_prefix("127.0.0.2");
        new_element_remote->mutable_prefix_len()->set_value(32);
      }
    } else {
      new_element_remote->set_address_prefix(
          Network::Test::getLoopbackAddressString(options.ip_version_));
      if (options.keylog_multiple_ips_) {
        auto more_remote = tls_keylog_remote->Add();
        if (options.ip_version_ == Network::Address::IpVersion::v6) {
          more_remote->set_address_prefix("1::2");
          more_remote->mutable_prefix_len()->set_value(128);
        } else {
          more_remote->set_address_prefix("127.0.0.2");
          more_remote->mutable_prefix_len()->set_value(32);
        }
      }
    }
  }
}

void ConfigHelper::initializeTls(
    const ServerSslOptions& options,
    envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
  common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
  common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

  auto* validation_context = common_tls_context.mutable_validation_context();
  if (options.custom_validator_config_) {
    validation_context->set_allocated_custom_validator_config(options.custom_validator_config_);
  } else {
    if (options.client_with_intermediate_cert_) {
      validation_context->add_verify_certificate_hash(TEST_CLIENT2_CERT_HASH);
      std::string cert_yaml = R"EOF(
        trusted_ca:
          filename: "{{ test_rundir }}/test/config/integration/certs/intermediate_ca_cert_chain.pem"
      )EOF";
      if (options.max_verify_depth_) {
        cert_yaml += R"EOF(
        max_verify_depth: 1
      )EOF";
      }
      TestUtility::loadFromYaml(TestEnvironment::substitute(cert_yaml), *validation_context);
    } else {
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      validation_context->add_verify_certificate_hash(
          options.expect_client_ecdsa_cert_ ? TEST_CLIENT_ECDSA_CERT_HASH : TEST_CLIENT_CERT_HASH);
    }
  }
  validation_context->set_allow_expired_certificate(options.allow_expired_certificate_);

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
    if (options.rsa_cert_ocsp_staple_) {
      tls_certificate->mutable_ocsp_staple()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/server_ocsp_resp.der"));
    }
  }
  if (options.ecdsa_cert_) {
    auto* tls_certificate = common_tls_context.add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsacert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/server_ecdsakey.pem"));
    if (options.ecdsa_cert_ocsp_staple_) {
      tls_certificate->mutable_ocsp_staple()->set_filename(TestEnvironment::runfilesPath(
          "test/config/integration/certs/server_ecdsa_ocsp_resp.der"));
    }
  }
  if (!options.san_matchers_.empty()) {
    *validation_context->mutable_match_typed_subject_alt_names() = {options.san_matchers_.begin(),
                                                                    options.san_matchers_.end()};
  }
  initializeTlsKeyLog(common_tls_context, options);
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

void ConfigHelper::addBootstrapExtension(const std::string& config) {
  RELEASE_ASSERT(!finalized_, "");
  auto* extension = bootstrap_.add_bootstrap_extensions();
  TestUtility::loadFromYaml(config, *extension);
}

bool ConfigHelper::loadHttpConnectionManager(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  return loadFilter<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
      "http", hcm);
}

void ConfigHelper::storeHttpConnectionManager(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        hcm) {
  return storeFilter<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
      "http", hcm);
}

void ConfigHelper::addConfigModifier(ConfigModifierFunction function) {
  RELEASE_ASSERT(!finalized_, "");
  config_modifiers_.push_back(std::move(function));
}

void ConfigHelper::addConfigModifier(HttpModifierFunction function) {
  addConfigModifier([function, this](envoy::config::bootstrap::v3::Bootstrap&) -> void {
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager
        hcm_config;
    if (!loadHttpConnectionManager(hcm_config)) {
      return;
    }
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

  const std::string lds_filename =
      bootstrap().dynamic_resources().lds_config().path_config_source().path();
  std::string file = TestEnvironment::writeStringToFileForTest(
      "new_lds_file", MessageUtil::getJsonStringFromMessageOrDie(lds));
  TestEnvironment::renameFile(file, lds_filename);
}

void ConfigHelper::setDownstreamOutboundFramesLimits(uint32_t max_all_frames,
                                                     uint32_t max_control_frames) {
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

void ConfigHelper::setUpstreamOutboundFramesLimits(uint32_t max_all_frames,
                                                   uint32_t max_control_frames) {
  addConfigModifier(
      [max_all_frames, max_control_frames](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        ConfigHelper::HttpProtocolOptions protocol_options;
        auto* http_protocol_options =
            protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
        http_protocol_options->mutable_max_outbound_frames()->set_value(max_all_frames);
        http_protocol_options->mutable_max_outbound_control_frames()->set_value(max_control_frames);
        ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                         protocol_options);
      });
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

void ConfigHelper::adjustUpstreamTimeoutForTsan(
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager& hcm) {
  auto* route =
      hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
  uint64_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(*route, timeout, 15000u);
  auto* timeout = route->mutable_timeout();
  // QUIC stream processing is slow under TSAN. Use larger timeout to prevent
  // response_timeout.
  timeout->set_seconds(TSAN_TIMEOUT_FACTOR * timeout_ms / 1000);
}

envoy::config::core::v3::Http3ProtocolOptions ConfigHelper::http2ToHttp3ProtocolOptions(
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
    size_t http3_max_stream_receive_window) {
  envoy::config::core::v3::Http3ProtocolOptions http3_options;
  if (http2_options.has_initial_stream_window_size() &&
      http2_options.initial_stream_window_size().value() < http3_max_stream_receive_window) {
    // Set http3 stream flow control window only if the configured http2 stream flow control
    // window is smaller than the upper limit of flow control window supported by QUICHE which is
    // also the default for http3 streams.
    http3_options.mutable_quic_protocol_options()->mutable_initial_stream_window_size()->set_value(
        http2_options.initial_stream_window_size().value());
  }
  if (http2_options.has_override_stream_error_on_invalid_http_message()) {
    http3_options.mutable_override_stream_error_on_invalid_http_message()->set_value(
        http2_options.override_stream_error_on_invalid_http_message().value());
  } else if (http2_options.stream_error_on_invalid_http_messaging()) {
    http3_options.mutable_override_stream_error_on_invalid_http_message()->set_value(true);
  }
  return http3_options;
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
