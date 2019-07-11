#include "test/server/config_validation/xds_fuzz.h"

#include <fstream>

#include "envoy/event/dispatcher.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/fake_upstream.h"

namespace Envoy {
namespace Server {

const std::string config = R"EOF(
dynamic_resources:
  lds_config: {ads: {}}
  cds_config: {ads: {}}
  ads_config:
    api_type: GRPC
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout: { seconds: 5 }
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF";

XdsFuzzTest::XdsFuzzTest(Network::Address::IpVersion version,
                         const test::server::config_validation::XdsTestCase& input)
    : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, version, config),
      actions_(input.actions()), num_lds_updates_(0) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;
}

void XdsFuzzTest::initialize() {
  // Add ADS config with gRPC.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    auto* grpc_service = ads_config->add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name("ads_cluster");
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
  });
  HttpIntegrationTest::initialize();
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
  if (xds_stream_ == nullptr) {
    createXdsConnection();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result, result.message());
    xds_stream_->startGrpcStream();
  }
}

envoy::api::v2::Listener XdsFuzzTest::buildListener(const std::string& name,
                                                    const std::string& route_config,
                                                    const std::string& stat_prefix) {
  return TestUtility::parseYaml<envoy::api::v2::Listener>(fmt::format(
      R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: envoy.http_connection_manager
          config:
            stat_prefix: {}
            codec_type: HTTP1
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.router }}]
    )EOF",
      name, Network::Test::getLoopbackAddressString(version_), stat_prefix, route_config));
}

envoy::api::v2::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return TestUtility::parseYaml<envoy::api::v2::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config: {{ eds_config: {{ ads: {{}} }} }}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                     name));
}

envoy::api::v2::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return TestUtility::parseYaml<envoy::api::v2::ClusterLoadAssignment>(
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
                  name, Network::Test::getLoopbackAddressString(version_),
                  fake_upstreams_[0]->localAddress()->ip()->port()));
}

envoy::api::v2::RouteConfiguration XdsFuzzTest::buildRouteConfig(const std::string& name,
                                                                 const std::string& cluster) {
  return TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                                                                name, cluster));
}

void XdsFuzzTest::updateListener(const std::vector<envoy::api::v2::Listener>& listeners,
                                 const std::string& version) {
  // Parse action and state into a DiscoveryResponse.
  ENVOY_LOG_MISC(debug, "Sending Listener DiscoveryResponse version {}", version);
  sendDiscoveryResponse<envoy::api::v2::Listener>(Config::TypeUrl::get().Listener, listeners, {},
                                                  {}, version);
}

void XdsFuzzTest::updateRoute(const std::vector<envoy::api::v2::RouteConfiguration> routes,
                              const std::string& version) {
  // Parse action and state into a DiscoveryResponse.
  sendDiscoveryResponse<envoy::api::v2::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, {}, {}, version);
}

void XdsFuzzTest::close() {
  cleanUpXdsConnection();
  test_server_.reset();
  fake_upstreams_.clear();
}

void XdsFuzzTest::replay() {
  initialize();

  // Set up an initial cluster.
  sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                 {buildCluster("cluster_0")},
                                                 {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::api::v2::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  for (const auto& action : actions_) {
    ENVOY_LOG_MISC(trace, "Action: {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      // Update the listener list.
      listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                     [&action](const envoy::api::v2::Listener& listener) {
                                       return listener.name() == action.add_listener().name();
                                     }),
                      listeners.end());
      listeners.push_back(
          buildListener(action.add_listener().name(), action.add_listener().route_config()));

      // Send Listener DiscoveryResponse and update count.
      updateListener(listeners, action.add_listener().version());
      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      // Update the route list.
      std::remove_if(routes.begin(), routes.end(),
                     [&](const envoy::api::v2::RouteConfiguration& route) {
                       return route.name() == action.remove_route().name();
                     });

      routes.push_back(buildRouteConfig(action.add_route().name(), action.add_route().cluster()));
      updateRoute(routes, action.add_route().version());
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      // Remove listener from list.
      listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                     [&action](const envoy::api::v2::Listener& listener) {
                                       return listener.name() == action.remove_listener().name();
                                     }),
                      listeners.end());
      // Send state of the world listener DiscoveryResponse.
      updateListener(listeners, action.add_listener().version());
      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kRemoveRoute: {
      // Check if route with this name is in the list of routes, and if so, remove.
      std::remove_if(routes.begin(), routes.end(),
                     [&](const envoy::api::v2::RouteConfiguration& route) {
                       return route.name() == action.remove_route().name();
                     });
      updateRoute(routes, action.add_route().version());
      break;
    }
    default:
      break;
    }
  }
  // Verify and disconnect.
  verifyState();
  close();
}

void XdsFuzzTest::verifyState() {
  // Verify listener states.
  EXPECT_EQ(test_server_->counter("listener_manager.lds.update_attempt")->value(),
            num_lds_updates_);
  // Check listener success.
}

} // namespace Server
} // namespace Envoy
