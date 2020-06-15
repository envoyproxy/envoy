#include "test/server/config_validation/xds_fuzz.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {

// helper functions to build API responses
envoy::config::cluster::v3::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return ConfigHelper::buildCluster(name);
}

envoy::config::endpoint::v3::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(name, Network::Test::getLoopbackAddressString(ip_version_), fake_upstreams_[0]->localAddress()->ip()->port());
}

envoy::config::listener::v3::Listener
XdsFuzzTest::buildListener(uint32_t listener_num, uint32_t route_num) {
  std::string name = fmt::format("{}{}", "listener_", listener_num % NUM_LISTENERS);
  std::string route = fmt::format("{}{}", "route_config_", route_num % NUM_ROUTES);
  return ConfigHelper::buildListener(name, Network::Test::getLoopbackAddressString(ip_version_), route);
}

envoy::config::route::v3::RouteConfiguration
XdsFuzzTest::buildRouteConfig(uint32_t route_num) {
  std::string route = fmt::format("{}{}", "route_config_", route_num % NUM_ROUTES);
  return ConfigHelper::buildRouteConfig(route, "cluster_0");
}

// helper functions to send API responses
void XdsFuzzTest::updateListener(const std::vector<envoy::config::listener::v3::Listener>& listeners,
                             const std::vector<envoy::config::listener::v3::Listener>& added_or_updated,
                             const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(debug, "Sending Listener DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener, listeners, added_or_updated,
                                                  removed, std::to_string(version_));
}

void XdsFuzzTest::updateRoute(const std::vector<envoy::config::route::v3::RouteConfiguration> routes,
                             const std::vector<envoy::config::route::v3::RouteConfiguration>& added_or_updated,
                             const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(debug, "Sending Route DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, added_or_updated, removed, std::to_string(version_));
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase &input)
    : HttpIntegrationTest(
          Http::CodecClient::Type::HTTP2, input.config().ip_version() == test::server::config_validation::Config::IPv4 ? Network::Address::IpVersion::v4 : Network::Address::IpVersion::v6,
          ConfigHelper::adsBootstrap(input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW ? "GRPC" : "DELTA_GRPC")),
          actions_(input.actions()), num_lds_updates_(0) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;

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

/**
 * initialize an envoy running ADS
 */
void XdsFuzzTest::initialize() {
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    auto* grpc_service = ads_config->add_grpc_services();

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

/**
 * remove a listener from the list of listeners if it exists
 * @param the listener number to be removed
 * @return the listener as an optional so that it can be used in a delta request
 */
absl::optional<std::string> XdsFuzzTest::removeListener(uint32_t listener_num) {
  std::string match = fmt::format("{}{}", "listener_", listener_num % NUM_LISTENERS);

  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->name() == match) {
      std::string name = it->name();
      /* envoy::config::listener::v3::Listener listener = *it; */
      listeners_.erase(it);
      return name;
      /* return listener; */
    }
  }
  return {};
  /* listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), */
  /*                                 [&match](const envoy::config::listener::v3::Listener& listener) { */
  /*                                   return listener.name() == match; */
  /*                                 }), */
  /*                  listeners_.end()); */
}

/**
 * remove a route from the list of routes if it exists
 * @param the route number to be removed
 * @return the route as an optional so that it can be used in a delta request
 */
absl::optional<std::string> XdsFuzzTest::removeRoute(uint32_t route_num) {
  std::string match = fmt::format("{}{}", "route_config_", route_num % NUM_ROUTES);
  /* routes_.erase(std::remove_if(routes_.begin(), routes_.end(), */
  /*                                 [&match](const envoy::config::route::v3::RouteConfiguration& route) { */
  /*                                   return route.name() == match; */
  /*                                 }), */
  /*                  routes_.end()); */
  for (auto it = routes_.begin(); it != routes_.end(); ++it) {
    if (it->name() == match) {
      std::string name = it->name();
      /* envoy::config::route::v3::RouteConfiguration route = *it; */
      routes_.erase(it);
      return name;
    }
  }
  return {};
}

/**
 * run the sequence of actions defined in the fuzzed protobuf
 */
void XdsFuzzTest::replay() {
  initialize();
  /* num_lds_updates_++; */
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "1");
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "1");

  // TODO(samflattery): compareDiscoveryResponse here?

  for (const auto& action : actions_) {
    ENVOY_LOG_MISC(trace, "Action: {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      removeListener(action.add_listener().listener_num());
      auto listener = buildListener(action.add_listener().listener_num(), action.add_listener().route_num());
      listeners_.push_back(listener);

      updateListener(listeners_, {listener}, {});

      //TODO(samflattery): compareDiscoveryResponse to check ACK/NACK?

      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      auto removed = removeListener(action.add_listener().listener_num());

      if (removed) {
        updateListener(listeners_, {}, {*removed});
      } else {
        updateListener(listeners_, {}, {});
      }

      num_lds_updates_++;
      test_server_->waitForCounterGe("listener_manager.lds.update_attempt", num_lds_updates_);
      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      removeRoute(action.add_route().route_num());
      auto route = buildRouteConfig(action.add_route().route_num());
      routes_.push_back(route);
      updateRoute(routes_, {route}, {});
      break;
    }
    case test::server::config_validation::Action::kRemoveRoute: {
      auto removed = removeRoute(action.remove_route().route_num());
      if (removed) {
        updateRoute(routes_, {}, {*removed});
      } else {
        updateRoute(routes_, {}, {});
      }
      break;
    }
    default:
      break;
    }
    version_++;
  }

  verifyState();
  close();
}

void XdsFuzzTest::verifyState() {
  EXPECT_EQ(test_server_->counter("listener_manager.lds.update_attempt")->value(),
            num_lds_updates_);
  //TODO(samflattery): check other stats
}


} // namespace Envoy
