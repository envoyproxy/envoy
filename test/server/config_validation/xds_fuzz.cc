#include "test/server/config_validation/xds_fuzz.h"

/* #include "envoy/admin/v3/config_dump.pb.h" */
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

/* #include "common/config/protobuf_link_hacks.h" */
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "test/fuzz/utility.h"
/* #include "test/fuzz/fuzz_runner.h" */

/* #include "test/test_common/network_utility.h" */
/* #include "test/test_common/resources.h" */
/* #include "test/test_common/utility.h" */

namespace Envoy {

envoy::config::cluster::v3::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return TestUtility::parseYaml<envoy::config::cluster::v3::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config: {{ eds_config: {{ ads: {{}} }} }}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                                 name));
}

envoy::config::endpoint::v3::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return TestUtility::parseYaml<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      fmt::format(R"EOF(
      cluster_name: {}
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: {}
    )EOF",
                  name, Network::Test::getLoopbackAddressString(ipVersion()),
                  fake_upstreams_[0]->localAddress()->ip()->port()));
}

envoy::config::listener::v3::Listener
XdsFuzzTest::buildListener(const std::string& name, const std::string& route_config,
                                  const std::string& stat_prefix) {
  return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(fmt::format(
      R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: http
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
            stat_prefix: {}
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.filters.http.router }}]
    )EOF",
      name, Network::Test::getLoopbackAddressString(ipVersion()), stat_prefix, route_config));
}

envoy::config::route::v3::RouteConfiguration
XdsFuzzTest::buildRouteConfig(const std::string& name, const std::string& cluster) {
  return TestUtility::parseYaml<envoy::config::route::v3::RouteConfiguration>(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                                                                          name,
                                                                                          cluster));
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase &input)
    : HttpIntegrationTest(
          Http::CodecClient::Type::HTTP2, ipVersion(),
          ConfigHelper::adsBootstrap(sotwOrDelta() == Grpc::SotwOrDelta::Sotw ? "GRPC" : "DELTA_GRPC")),
          actions_(input.actions()) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = true;
  sotw_or_delta_ = sotwOrDelta();
}

void XdsFuzzTest::initialize() {
  const bool rate_limiting = false;
  config_helper_.addConfigModifier([this](
                                       envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    if (rate_limiting) {
      ads_config->mutable_rate_limit_settings();
    }
    auto* grpc_service = ads_config->add_grpc_services();
    setGrpcService(*grpc_service, "ads_cluster", xds_upstream_->localAddress());
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext context;
    auto* validation_context = context.mutable_common_tls_context()->mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    validation_context->add_match_subject_alt_names()->set_suffix("lyft.com");
    if (clientType() == Grpc::ClientType::GoogleGrpc) {
      auto* google_grpc = grpc_service->mutable_google_grpc();
      auto* ssl_creds = google_grpc->mutable_channel_credentials()->mutable_ssl_credentials();
      ssl_creds->mutable_root_certs()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
    }
    ads_cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
    ads_cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(context);
  });
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);
  HttpIntegrationTest::initialize();
  if (xds_stream_ == nullptr) {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
}

void XdsFuzzTest::TearDown() {
  cleanUpXdsConnection();
  test_server_.reset();
  fake_upstreams_.clear();
}

void XdsFuzzTest::replay() {
  initialize();
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");
}

} // namespace Envoy
