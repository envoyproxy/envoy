#include "test/server/config_validation/xds_fuzz.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

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
                  name, Network::Test::getLoopbackAddressString(ip_version_),
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
      name, Network::Test::getLoopbackAddressString(ip_version_), stat_prefix, route_config));
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

void XdsFuzzTest::updateListener(const std::vector<envoy::config::listener::v3::Listener>& listeners,
                                 const std::string& version) {
  // Parse action and state into a DiscoveryResponse.
  ENVOY_LOG_MISC(debug, "Sending Listener DiscoveryResponse version {}", version);
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener, listeners, {},
                                                  {}, version);
}

void XdsFuzzTest::updateRoute(const std::vector<envoy::config::route::v3::RouteConfiguration> routes,
                              const std::string& version) {
  // Parse action and state into a DiscoveryResponse.
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, {}, {}, version);
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase &input)
    : HttpIntegrationTest(
          Http::CodecClient::Type::HTTP2, Network::Address::IpVersion::v4,
          /* ConfigHelper::adsBootstrap("GRPC")), */
          ConfigHelper::adsBootstrap(input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW ? "GRPC" : "DELTA_GRPC")),
          actions_(input.actions()), num_lds_updates_(0) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;
  /* ip_version_ = Network::Address::IpVersion::v4; */
  /* client_type_ = Grpc::ClientType::EnvoyGrpc; */
  /* sotw_or_delta_ = Grpc::SotwOrDelta::Sotw; */

  ENVOY_LOG_MISC(debug, "{}", input.DebugString());

  parseConfig(input);
}

void XdsFuzzTest::parseConfig(const test::server::config_validation::XdsTestCase &input) {
  if (input.config().ip_version() == test::server::config_validation::Config::IPv4) {
      ip_version_ = Network::Address::IpVersion::v4;
  } else {
      ip_version_ = Network::Address::IpVersion::v6;
  }

  if (input.config().client_type() == test::server::config_validation::Config::GOOGLE_GRPC) {
    client_type_ = Grpc::ClientType::GoogleGrpc;
  } else {
    client_type_ = Grpc::ClientType::EnvoyGrpc;
  }

  if (input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
  } else {
    sotw_or_delta_ = Grpc::SotwOrDelta::Delta;
  }
}

void XdsFuzzTest::initialize() {
  // Add ADS config with gRPC.
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    auto* grpc_service = ads_config->add_grpc_services();

    /* grpc_service->mutable_envoy_grpc()->set_cluster_name("ads_cluster"); */
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    std::string cluster_name = "ads_cluster";
    switch (client_type_) {
    case Grpc::ClientType::EnvoyGrpc:
        grpc_service->mutable_envoy_grpc()->set_cluster_name(cluster_name);
        break;
    case Grpc::ClientType::GoogleGrpc: {
        auto* google_grpc = grpc_service->mutable_google_grpc();
        google_grpc->set_target_uri(xds_upstream_->localAddress()->asString());
        google_grpc->set_stat_prefix(cluster_name);
        break;
    }
    default:
        NOT_REACHED_GCOVR_EXCL_LINE;
    }
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
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

void XdsFuzzTest::close() {
  cleanUpXdsConnection();
  test_server_.reset();
  fake_upstreams_.clear();
}

void XdsFuzzTest::replay() {
  initialize();
  num_lds_updates_++;
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  for (const auto& action : actions_) {
    ENVOY_LOG_MISC(trace, "Action: {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      // Update the listener list.
      listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                     [&action](const envoy::config::listener::v3::Listener& listener) {
                                       return listener.name() == action.add_listener().name();
                                     }),
                      listeners_.end());
      listeners_.push_back(
          buildListener(action.add_listener().name(), action.add_listener().route_config()));

      // Send Listener DiscoveryResponse and update count.
      updateListener(listeners_, action.add_listener().version());
      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      // Update the route list.
      routes_.erase(std::remove_if(routes_.begin(), routes_.end(),
                                   [&action](const envoy::config::route::v3::RouteConfiguration& route) {
                                     return route.name() == action.remove_route().name();
                                   }),
                    routes_.end());

      routes_.push_back(buildRouteConfig(action.add_route().name(), action.add_route().cluster()));
      updateRoute(routes_, action.add_route().version());
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      // Remove listener from list.
      listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                     [&action](const envoy::config::listener::v3::Listener& listener) {
                                       return listener.name() == action.remove_listener().name();
                                     }),
                      listeners_.end());
      // Send state of the world listener DiscoveryResponse.
      updateListener(listeners_, action.remove_listener().version());
      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kRemoveRoute: {
      // Check if route with this name is in the list of routes, and if so, remove.
      routes_.erase(std::remove_if(routes_.begin(), routes_.end(),
                                   [&action](const envoy::config::route::v3::RouteConfiguration& route) {
                                     return route.name() == action.remove_route().name();
                                  }),
                    routes_.end());
      updateRoute(routes_, action.remove_route().version());
      break;
    }
    default:
      break;
    }
  }


  close();
}

} // namespace Envoy
