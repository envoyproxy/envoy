#include "test/server/config_validation/xds_fuzz.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"

namespace Envoy {

// helper functions to build API responses
envoy::config::cluster::v3::Cluster XdsFuzzTest::buildCluster(const std::string& name) {
  return ConfigHelper::buildCluster(name, "ROUND_ROBIN", api_version_);
};

envoy::config::endpoint::v3::ClusterLoadAssignment
XdsFuzzTest::buildClusterLoadAssignment(const std::string& name) {
  return ConfigHelper::buildClusterLoadAssignment(
      name, Network::Test::getLoopbackAddressString(ip_version_),
      fake_upstreams_[0]->localAddress()->ip()->port(), api_version_);
}

envoy::config::listener::v3::Listener XdsFuzzTest::buildListener(uint32_t listener_num,
                                                                 uint32_t route_num) {
  std::string name = absl::StrCat("listener_", listener_num % ListenersMax);
  std::string route = absl::StrCat("route_config_", route_num % RoutesMax);
  return ConfigHelper::buildListener(
      name, route, Network::Test::getLoopbackAddressString(ip_version_), "ads_test", api_version_);
}

envoy::config::route::v3::RouteConfiguration XdsFuzzTest::buildRouteConfig(uint32_t route_num) {
  std::string route = absl::StrCat("route_config_", route_num % RoutesMax);
  return ConfigHelper::buildRouteConfig(route, "cluster_0", api_version_);
}

// helper functions to send API responses
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
    const std::vector<envoy::config::route::v3::RouteConfiguration> routes,
    const std::vector<envoy::config::route::v3::RouteConfiguration>& added_or_updated,
    const std::vector<std::string>& removed) {
  ENVOY_LOG_MISC(debug, "Sending Route DiscoveryResponse version {}", version_);
  sendDiscoveryResponse<envoy::config::route::v3::RouteConfiguration>(
      Config::TypeUrl::get().RouteConfiguration, routes, added_or_updated, removed,
      std::to_string(version_));
}

XdsFuzzTest::XdsFuzzTest(const test::server::config_validation::XdsTestCase& input,
                         envoy::config::core::v3::ApiVersion api_version)
    : HttpIntegrationTest(
          Http::CodecClient::Type::HTTP2, TestEnvironment::getIpVersionsForTest()[0],
          ConfigHelper::adsBootstrap(input.config().sotw_or_delta() ==
                                             test::server::config_validation::Config::SOTW
                                         ? "GRPC"
                                         : "DELTA_GRPC",
                                     api_version)),
      actions_(input.actions()), version_(1), api_version_(api_version),
      ip_version_(TestEnvironment::getIpVersionsForTest()[0]) {
  use_lds_ = false;
  create_xds_upstream_ = true;
  tls_xds_upstream_ = false;

  if (input.config().sotw_or_delta() == test::server::config_validation::Config::SOTW) {
    sotw_or_delta_ = Grpc::SotwOrDelta::Sotw;
  } else {
    sotw_or_delta_ = Grpc::SotwOrDelta::Delta;
  }
}

/**
 * initialize an envoy configured with a fully dynamic bootstrap with ADS over gRPC
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
  std::string match = absl::StrCat("listener_", listener_num % ListenersMax);

  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    if (it->name() == match) {
      listeners_.erase(it);
      return match;
    }
  }
  return {};
}

/**
 * remove a route from the list of routes if it exists
 * @param the route number to be removed
 * @return the route as an optional so that it can be used in a delta request
 */
absl::optional<std::string> XdsFuzzTest::removeRoute(uint32_t route_num) {
  std::string match = absl::StrCat("route_config_", route_num % RoutesMax);
  for (auto it = routes_.begin(); it != routes_.end(); ++it) {
    if (it->name() == match) {
      routes_.erase(it);
      return match;
    }
  }
  return {};
}

/**
 * wait for a specific ACK, ignoring any other ACKs that are made in the meantime
 * @param the expected API type url of the ack
 * @param the expected version number
 * @return AssertionSuccess() if the ack was received, else an AssertionError()
 */
AssertionResult XdsFuzzTest::waitForAck(const std::string& expected_type_url,
                                        const std::string& expected_version) {
  if (sotw_or_delta_ == Grpc::SotwOrDelta::Sotw) {
    API_NO_BOOST(envoy::api::v2::DiscoveryRequest) discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, discovery_request));
      ENVOY_LOG_MISC(info, "Received gRPC message with type {} and version {}",
                     discovery_request.type_url(), discovery_request.version_info());
    } while (expected_type_url != discovery_request.type_url() ||
             expected_version != discovery_request.version_info());
  } else {
    API_NO_BOOST(envoy::api::v2::DeltaDiscoveryRequest) delta_discovery_request;
    do {
      VERIFY_ASSERTION(xds_stream_->waitForGrpcMessage(*dispatcher_, delta_discovery_request));
      ENVOY_LOG_MISC(info, "Received gRPC message with type {}",
                     delta_discovery_request.type_url());
    } while (expected_type_url != delta_discovery_request.type_url());
  }
  version_++;
  return AssertionSuccess();
}

/**
 * run the sequence of actions defined in the fuzzed protobuf
 */
void XdsFuzzTest::replay() {
  initialize();

  // set up cluster
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().Cluster, "", {}, {}, {}, true));
  sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(Config::TypeUrl::get().Cluster,
                                                             {buildCluster("cluster_0")},
                                                             {buildCluster("cluster_0")}, {}, "0");
  EXPECT_TRUE(compareDiscoveryRequest(Config::TypeUrl::get().ClusterLoadAssignment, "",
                                      {"cluster_0"}, {"cluster_0"}, {}));
  sendDiscoveryResponse<envoy::config::endpoint::v3::ClusterLoadAssignment>(
      Config::TypeUrl::get().ClusterLoadAssignment, {buildClusterLoadAssignment("cluster_0")},
      {buildClusterLoadAssignment("cluster_0")}, {}, "0");

  // the client will not subscribe to the RouteConfiguration type URL until it
  // receives a listener, and the ACKS it sends back seem to be an empty type
  // URL so just don't check them until a listener is added
  bool sent_listener = false;

  uint32_t added = 0;
  uint32_t modified = 0;
  uint32_t removed = 0;

  for (const auto& action : actions_) {
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kAddListener: {
      ENVOY_LOG_MISC(info, "Adding listener_{} with reference to route_{}",
                     action.add_listener().listener_num() % ListenersMax,
                     action.add_listener().route_num() % RoutesMax);
      uint32_t listener_num = action.add_listener().listener_num();
      auto removed_name = removeListener(listener_num);
      auto listener = buildListener(listener_num, action.add_listener().route_num());
      listeners_.push_back(listener);

      updateListener(listeners_, {listener}, {});
      // use waitForAck instead of compareDiscoveryRequest as the client makes
      // additional discoveryRequests at launch that we might not want to
      // respond to yet
      EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
      if (removed_name) {
        modified++;
        verifier_.listenerUpdated(listener);
        test_server_->waitForCounterGe("listener_manager.listener_modified", modified);
      } else {
        added++;
        verifier_.listenerAdded(listener);
        test_server_->waitForCounterGe("listener_manager.listener_added", added);
      }

      if (!sent_listener) {
        ENVOY_LOG_MISC(info, "Adding route_{}", action.add_listener().route_num() % RoutesMax);
        uint32_t route_num = action.add_listener().route_num();
        auto removed_name = removeRoute(route_num);
        auto route = buildRouteConfig(route_num);
        routes_.push_back(route);

        if (removed_name) {
          // if the route was already in routes_, don't send a duplicate add in delta request
          updateRoute(routes_, {}, {});
          verifier_.routeUpdated(route);
        } else {
          updateRoute(routes_, {route}, {});
          verifier_.routeAdded(route);
        }

        EXPECT_TRUE(waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
      }
      sent_listener = true;
      break;
    }
    case test::server::config_validation::Action::kRemoveListener: {
      ENVOY_LOG_MISC(info, "Removing listener_{}",
                     action.remove_listener().listener_num() % ListenersMax);
      auto removed_name = removeListener(action.remove_listener().listener_num());

      if (removed_name) {
        removed++;
        updateListener(listeners_, {}, {*removed_name});
        EXPECT_TRUE(waitForAck(Config::TypeUrl::get().Listener, std::to_string(version_)));
        verifier_.listenerRemoved(*removed_name);
        test_server_->waitForCounterGe("listener_manager.listener_removed", removed);
      }

      break;
    }
    case test::server::config_validation::Action::kAddRoute: {
      if (!sent_listener) {
        ENVOY_LOG_MISC(info, "Ignoring request to add route_{}", action.add_route().route_num());
        break;
      }
      ENVOY_LOG_MISC(info, "Adding route_{}", action.add_route().route_num() % RoutesMax);
      uint32_t route_num = action.add_route().route_num();
      auto removed_name = removeRoute(route_num);
      auto route = buildRouteConfig(route_num);
      routes_.push_back(route);

      if (removed_name) {
        // if the route was already in routes_, don't send a duplicate add in delta request
        updateRoute(routes_, {}, {});
        verifier_.routeUpdated(route);
      } else {
        updateRoute(routes_, {route}, {});
        verifier_.routeAdded(route);
      }

      EXPECT_TRUE(waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
      break;
    }
    case test::server::config_validation::Action::kRemoveRoute: {
      // it seems like routes cannot be removed - leaving a route out of an SOTW request does not
      // remove it and sending a remove message in a delta request is ignored
      ENVOY_LOG_MISC(info, "Ignoring request to remove route_{}",
                     action.remove_route().route_num());
      break;

      // TODO(samflattery): remove if it's true that routes cannot be removed
      auto removed_name = removeRoute(action.remove_route().route_num());
      if (removed) {
        updateRoute(routes_, {}, {*removed_name});
        verifier_.routeRemoved(*removed_name);
        EXPECT_TRUE(
            waitForAck(Config::TypeUrl::get().RouteConfiguration, std::to_string(version_)));
      }
      break;
    }
    default:
      break;
    }
  }

  verifyState();
  close();
}

/**
 * verify that each listener in the verifier has a matching listener in the config dump
 */
void XdsFuzzTest::verifyListeners() {
  ENVOY_LOG_MISC(info, "Verifying listeners");
  const auto& listeners = verifier_.listeners();
  envoy::admin::v3::ListenersConfigDump listener_dump = getListenersConfigDump();
  ENVOY_LOG_MISC(info, "{}", listener_dump.DebugString());
  for (auto& listener_rep : listeners) {
    ENVOY_LOG_MISC(info, "Verifying {} with state {}", listener_rep.listener.name(),
                   listener_rep.state);
    bool found = false;
    for (auto& dump_listener : listener_dump.dynamic_listeners()) {
      if (dump_listener.name() == listener_rep.listener.name()) {
        ENVOY_LOG_MISC(info, "Found a matching listener in config dump");
        ENVOY_LOG_MISC(info, "drain: {}, warm {}, active {}", dump_listener.has_draining_state(),
                       dump_listener.has_warming_state(), dump_listener.has_active_state());
        switch (listener_rep.state) {
        case XdsVerifier::DRAINING:
          if (dump_listener.has_draining_state()) {
            ENVOY_LOG_MISC(info, "{} is draining", listener_rep.listener.name());
            found = true;
          }
          break;
        case XdsVerifier::WARMING:
          if (dump_listener.has_warming_state()) {
            ENVOY_LOG_MISC(info, "{} is warming", listener_rep.listener.name());
            found = true;
          }
          break;
        case XdsVerifier::ACTIVE:
          if (dump_listener.has_active_state()) {
            ENVOY_LOG_MISC(info, "{} is active", listener_rep.listener.name());
            found = true;
          }
          break;
        default:
          NOT_REACHED_GCOVR_EXCL_LINE;
        }
      }
    }
    if (!found) {
      ENVOY_LOG_MISC(info, "Expected to find listener {} in config dump",
                     listener_rep.listener.name());
      throw EnvoyException(fmt::format("Expected to find listener {} in config dump",
                                        listener_rep.listener.name()));
    }
  }
}

void XdsFuzzTest::verifyState() {
  verifyListeners();
  ENVOY_LOG_MISC(info, "Verified listeners");

  EXPECT_EQ(test_server_->gauge("listener_manager.total_listeners_draining")->value(),
            verifier_.numDraining());
  EXPECT_EQ(test_server_->gauge("listener_manager.total_listeners_warming")->value(),
            verifier_.numWarming());
  EXPECT_EQ(test_server_->gauge("listener_manager.total_listeners_active")->value(),
            verifier_.numActive());
  ENVOY_LOG_MISC(info, "Verified stats");
}

envoy::admin::v3::ListenersConfigDump XdsFuzzTest::getListenersConfigDump() {
  auto message_ptr =
      test_server_->server().admin().getConfigTracker().getCallbacksMap().at("listeners")();
  return dynamic_cast<const envoy::admin::v3::ListenersConfigDump&>(*message_ptr);
}

} // namespace Envoy
