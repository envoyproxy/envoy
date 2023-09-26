#include "test/server/config_validation/xds_fuzz.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

#include "source/common/common/matchers.h"

namespace Envoy {

// Helper functions to build API responses.
envoy::config::cluster::v3::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return ConfigHelper::buildCluster(name);
};

envoy::config::endpoint::v3::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(
      name, Network::Test::getLoopbackAddressString(ip_version_),
      fake_upstreams_[0]->localAddress()->ip()->port());
}

envoy::config::listener::v3::Listener XdsFuzzTest::buildListener(const std::string& listener_name,
                                                                 const std::string& route_name) {
  return ConfigHelper::buildListener(
      listener_name, route_name, Network::Test::getLoopbackAddressString(ip_version_), "ads_test");
}

envoy::config::route::v3::RouteConfiguration
XdsFuzzTest::buildRouteConfig(const std::string& route_name) {
  return ConfigHelper::buildRouteConfig(route_name, "cluster_0");
}

// Helper functions to send API responses.
void XdsFuzzTest::updateListener(
    const std::vector<envoy::config::listener::v3::Listener>& listeners,
    const std::vector<envoy::config::listener::v3::Listener>& added_or_updated,
    const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(debug, "Sending Listener DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::listener::v3::Listener>(Config::TypeUrl::get().Listener,
                                                               listeners, added_or_updated, removed,
                                                               std::to_string(version_));
}

void XdsFuzzTest::updateRoute(
    const std::vector<envoy::config::route::v3::RouteConfiguration>& routes,
    const std::vector<envoy::config::route::v3::RouteConfiguration>& added_or_updated,
    const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(debug, "Sending Route DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, added_or_updated, removed,
      std::to_string(version_));
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase& input,
                         bool use_unified_mux)
    : HttpIntegrationTest(
          Http::CodecType::HTTP2, TestEnvironment::getIpVersionsForTest()[0],
          ConfigHelper::adsBootstrap(input.config().sotw_or_delta() ==
                                             test::server::config_validation::Config::SOTW
                                         ? "GRPC"
                                         : "DELTA_GRPC")),
      verifier_(input.config().sotw_or_delta()), actions_(input.actions()),
      ip_version_(TestEnvironment::getIpVersionsForTest()[0]) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux",
                                    use_unified_mux ? "true" : "false");
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;

  // Avoid listeners draining during the test.
  drain_time_ = std::chrono::seconds(60);

  if (input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = use_unified_mux ? Grpc::SotwOrDelta::UnifiedSotw : Grpc::SotwOrDelta::Sotw;
  } else {
    sotw_or_delta_ = use_unified_mux ? Grpc::SotwOrDelta::UnifiedDelta : Grpc::SotwOrDelta::Delta;
  }
}

/**
 * Initialize an envoy configured with a fully dynamic bootstrap with ADS over gRPC.
 */
void XdsFuzzTest::initialize() {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
    auto* grpc_service = ads_config->add_grpc_services();

    std::string cluster_name = "ads_cluster";
    grpc_service->mutable_envoy_grpc()->set_cluster_name(cluster_name);
    auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
    ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
    ads_cluster->set_name("ads_cluster");
  });
  setUpstreamProtocol(Http::CodecType::HTTP2);
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
 * @return true iff listener_name is in listeners_ (and removes it from listeners_)
 */
bool XdsFuzzTest::eraseListener(const std::string& listener_name) {
  const auto orig_size = listeners_.size();
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](auto& listener) { return listener.name() == listener_name; }),
                   listeners_.end());
  return orig_size != listeners_.size();
}

/**
 * @return true iff route_name has already been added to routes_
 */
bool XdsFuzzTest::hasRoute(const std::string& route_name) {
  return std::any_of(routes_.begin(), routes_.end(),
                     [&](auto& route) { return route.name() == route_name; });
}

/**
 * Log the current state of the verifier and the test server for debugging purposes to see
 * discrepancies.
 */
void XdsFuzzTest::logState() {
  ENVOY_LOG_MISC(debug, "warming {} ({}), active {} ({}), draining {} ({})", verifier_.numWarming(),
                 test_server_->gauge("listener_manager.total_listeners_warming")->value(),
                 verifier_.numActive(),
                 test_server_->gauge("listener_manager.total_listeners_active")->value(),
                 verifier_.numDraining(),
                 test_server_->gauge("listener_manager.total_listeners_draining")->value());
  ENVOY_LOG_MISC(
      debug, "added {} ({}), modified {} ({}), removed {} ({})", verifier_.numAdded(),
      test_server_->counter("listener_manager.listener_added")->value(), verifier_.numModified(),
      test_server_->counter("listener_manager.listener_modified")->value(), verifier_.numRemoved(),
      test_server_->counter("listener_manager.listener_removed")->value());
}

/**
 * Send an xDS response to add a listener and update state accordingly.
 */
void XdsFuzzTest::addListener(const std::string& listener_name, const std::string& route_name) {
  ENVOY_LOG_MISC(debug, "Adding {} with reference to {}", listener_name, route_name);
  lds_update_success_++;
  bool removed = eraseListener(listener_name);
  auto listener = buildListener(listener_name, route_name);
  listeners_.push_back(listener);

  updateListener(listeners_, {listener}, {});

  // Use waitForAck instead of compareDiscoveryRequest as the client makes additional
  // DiscoveryRequests at launch that we might not want to respond to yet.
  EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
  if (removed) {
    verifier_.listenerUpdated(listener);
  } else {
    verifier_.listenerAdded(listener);
  }
}

/**
 * Send an xDS response to remove a listener and update state accordingly.
 */
void XdsFuzzTest::removeListener(const std::string& listener_name) {
  ENVOY_LOG_MISC(debug, "Removing {}", listener_name);
  bool removed = eraseListener(listener_name);

  if (removed) {
    lds_update_success_++;
    updateListener(listeners_, {}, {listener_name});
    EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
    verifier_.listenerRemoved(listener_name);
  }
}

/**
 * Send an xDS response to add a route and update state accordingly.
 */
void XdsFuzzTest::addRoute(const std::string& route_name) {
  ENVOY_LOG_MISC(debug, "Adding {}", route_name);
  auto route = buildRouteConfig(route_name);

  if (!hasRoute(route_name)) {
    routes_.push_back(route);
  }

  updateRoute(routes_, {route}, {});
  verifier_.routeAdded(route);

  EXPECT_TRUE(waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
}

/**
 * Wait for a specific ACK, ignoring any other ACKs that are made in the meantime.
 * @param the expected API type url of the ack
 * @param the expected version number
 * @return AssertionSuccess() if the ack was received, else an AssertionError()
 */
AssertionResult XdsFuzzTest::waitForAck(const std::string& expected_type_url,
                                        const std::string& expected_version) {
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw ||
      sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw) {
    envoy::service::discovery::v3::DiscoveryRequest discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
      ENVOY_LOG_MISC(debug, "Received gRPC message with type {} and version {}",
                     discovery_request.type_url(), discovery_request.version_info());
    } while (expected_type_url != discovery_request.type_url() ||
             expected_version != discovery_request.version_info());
  } else {
    envoy::service::discovery::v3::DeltaDiscoveryRequest delta_discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
      ENVOY_LOG_MISC(debug, "Received gRPC message with type {}",
                     delta_discovery_request.type_url());
    } while (expected_type_url != delta_discovery_request.type_url());
  }
  version_++;
  return AssertionSuccess();
}

/**
 * Run the sequence of actions defined in the fuzzed protobuf.
 */
void XdsFuzzTest::replay() {
  initialize();

  // Set up cluster.
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "0");
  // TODO (dmitri-d) legacy delta sends node with every DiscoveryRequest, other mux implementations
  // follow set_node_on_first_message_only config flag
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {},
                                      sotw_or_delta_ == Grpc::SotwOrDelta::Delta));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "0");

  // The client will not subscribe to the RouteConfiguration type URL until it receives a listener,
  // and the ACKS it sends back seem to be an empty type URL so just don't check them until a
  // listener is added.
  bool sent_listener = false;

  for (const auto& action : actions_) {
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      std::string listener_name =
          absl::StrCat("listener_", action.add_listener().listener_num() % ListenersMax);
      std::string route_name =
          absl::StrCat("route_config_", action.add_listener().route_num() % RoutesMax);
      addListener(listener_name, route_name);
      if (!sent_listener) {
        addRoute(route_name);
        test_server_->waitForCounterEq("listener_manager.listener_create_success", 1, timeout_);
      }
      sent_listener = true;
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      std::string listener_name =
          absl::StrCat("listener_", action.remove_listener().listener_num() % ListenersMax);
      removeListener(listener_name);
      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      if (!sent_listener) {
        ENVOY_LOG_MISC(debug, "Ignoring request to add route_{}",
                       action.add_route().route_num() % RoutesMax);
        break;
      }
      std::string route_name =
          absl::StrCat("route_config_", action.add_route().route_num() % RoutesMax);
      addRoute(route_name);
      break;
    }
    default:
      break;
    }
    if (sent_listener) {
      // Wait for all of the updates to take effect.
      test_server_->waitForGaugeEq("listener_manager.total_listeners_warming",
                                   verifier_.numWarming(), timeout_);
      test_server_->waitForGaugeEq("listener_manager.total_listeners_active", verifier_.numActive(),
                                   timeout_);
      test_server_->waitForGaugeEq("listener_manager.total_listeners_draining",
                                   verifier_.numDraining(), timeout_);
      test_server_->waitForCounterEq("listener_manager.listener_modified", verifier_.numModified(),
                                     timeout_);
      test_server_->waitForCounterEq("listener_manager.listener_added", verifier_.numAdded(),
                                     timeout_);
      test_server_->waitForCounterEq("listener_manager.listener_removed", verifier_.numRemoved(),
                                     timeout_);
      test_server_->waitForCounterEq("listener_manager.lds.update_success", lds_update_success_,
                                     timeout_);
    }
    logState();
  }

  verifyState();
  close();
}

/**
 * Verify that each listener in the verifier has a matching listener in the config dump.
 */
void XdsFuzzTest::verifyListeners() {
  ENVOY_LOG_MISC(debug, "Verifying listeners");
  const auto& abstract_rep = verifier_.listeners();
  const auto dump = getListenersConfigDump().dynamic_listeners();

  for (const auto& rep : abstract_rep) {
    ENVOY_LOG_MISC(debug, "Verifying {} with state {}", rep.listener.name(), rep.state);

    auto listener_dump = std::find_if(dump.begin(), dump.end(), [&](auto& listener) {
      return listener.name() == rep.listener.name();
    });

    // There should be a listener of the same name in the dump.
    if (listener_dump == dump.end()) {
      throw EnvoyException(fmt::format("Expected to find {} in config dump", rep.listener.name()));
    }

    // The state should match.
    switch (rep.state) {
    case XdsVerifier::DRAINING:
      FUZZ_ASSERT(listener_dump->has_draining_state());
      break;
    case XdsVerifier::WARMING:
      FUZZ_ASSERT(listener_dump->has_warming_state());
      break;
    case XdsVerifier::ACTIVE:
      FUZZ_ASSERT(listener_dump->has_active_state());
      break;
    default:
      PANIC("reached unexpected code");
    }
  }
}

void XdsFuzzTest::verifyRoutes() {
  auto dump = getRoutesConfigDump();

  // Go through routes in verifier and make sure each is in the config dump.
  auto routes = verifier_.routes();
  FUZZ_ASSERT(routes.size() == dump.size());
  for (const auto& route : routes) {
    FUZZ_ASSERT(std::any_of(dump.begin(), dump.end(), [&](const auto& dump_route) {
      return route.first == dump_route.name();
    }));
  }
}

void XdsFuzzTest::verifyState() {
  verifyListeners();
  ENVOY_LOG_MISC(debug, "Verified listeners");
  verifyRoutes();
  ENVOY_LOG_MISC(debug, "Verified routes");

  FUZZ_ASSERT(test_server_->gauge("listener_manager.total_listeners_draining")->value() ==
              verifier_.numDraining());
  FUZZ_ASSERT(test_server_->gauge("listener_manager.total_listeners_warming")->value() ==
              verifier_.numWarming());
  FUZZ_ASSERT(test_server_->gauge("listener_manager.total_listeners_active")->value() ==
              verifier_.numActive());
  ENVOY_LOG_MISC(debug, "Verified stats");
  ENVOY_LOG_MISC(debug, "warming {} ({}), active {} ({}), draining {} ({})", verifier_.numWarming(),
                 test_server_->gauge("listener_manager.total_listeners_warming")->value(),
                 verifier_.numActive(),
                 test_server_->gauge("listener_manager.total_listeners_active")->value(),
                 verifier_.numDraining(),
                 test_server_->gauge("listener_manager.total_listeners_draining")->value());
}

envoy::admin::v3::ListenersConfigDump XdsFuzzTest::getListenersConfigDump() {
  auto message_ptr = test_server_->server().admin()->getConfigTracker().getCallbacksMap().at(
      "listeners")(Matchers::UniversalStringMatcher());
  return dynamic_cast<const envoy::admin::v3::ListenersConfigDump&>(*message_ptr);
}

std::vector<envoy::config::route::v3::RouteConfiguration> XdsFuzzTest::getRoutesConfigDump() {
  auto map = test_server_->server().admin()->getConfigTracker().getCallbacksMap();

  // There is no route config dump before envoy has a route.
  if (map.find("routes") == map.end()) {
    return {};
  }

  auto message_ptr = map.at("routes")(Matchers::UniversalStringMatcher());
  auto dump = dynamic_cast<const envoy::admin::v3::RoutesConfigDump&>(*message_ptr);

  // Since the route config dump gives the RouteConfigurations as an Any, go through and cast them
  // back to RouteConfigurations.
  std::vector<envoy::config::route::v3::RouteConfiguration> dump_routes;
  for (const auto& route : dump.dynamic_route_configs()) {
    envoy::config::route::v3::RouteConfiguration dyn_route;
    route.route_config().UnpackTo(&dyn_route);
    dump_routes.push_back(dyn_route);
  }
  return dump_routes;
}

} // namespace Envoy
