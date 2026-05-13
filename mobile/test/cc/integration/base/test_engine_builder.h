#pragma once

#include "library/cc/engine_builder_base.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/extensions/clusters/static/static_cluster.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

namespace Envoy {
namespace Platform {

class TestEngineBuilder : public EngineBuilderBase<TestEngineBuilder> {
  friend class EngineBuilderBase<TestEngineBuilder>;

public:
  TestEngineBuilder() = default;

  TestEngineBuilder& addHcmHttpFilter(
      ::envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter) {
    hcm_http_filters_.push_back(std::move(filter));
    return *this;
  }

  TestEngineBuilder&
  setHcmRouteConfiguration(::envoy::config::route::v3::RouteConfiguration route_configuration) {
    route_configuration_ = std::move(route_configuration);
    return *this;
  }

  TestEngineBuilder& addCluster(::envoy::config::cluster::v3::Cluster cluster) {
    clusters_.push_back(std::move(cluster));
    return *this;
  }

private:
  // Hooks required by EngineBuilderBase
  void preRunSetup(InternalEngine* engine) {
    (void)engine;
    Envoy::Upstream::forceRegisterStaticClusterFactory();
    Envoy::Extensions::LoadBalancingPolicies::RoundRobin::forceRegisterFactory();
  }
  void postRunSetup(Engine*) {}

  absl::Status configXds(envoy::config::bootstrap::v3::Bootstrap*) { return absl::OkStatus(); }
  absl::Status configureNode(envoy::config::core::v3::Node* node) {
    node->set_id("envoy-test-client");
    node->set_cluster("envoy-test-client");
    return absl::OkStatus();
  }

  absl::Status configureRouteConfig(envoy::config::route::v3::RouteConfiguration* route_config) {
    if (route_configuration_.has_value()) {
      route_config->Swap(&route_configuration_.value());
    }
    return absl::OkStatus();
  }

  absl::Status configureStaticClusters(
      Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster>* clusters) {
    for (auto& cluster : clusters_) {
      *clusters->Add() = std::move(cluster);
    }
    clusters_.clear();
    return absl::OkStatus();
  }

  absl::Status configureHttpFilters(
      std::function<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter*()>
          add_filter) {
    for (auto& filter : hcm_http_filters_) {
      *add_filter() = std::move(filter);
    }
    hcm_http_filters_.clear();
    return absl::OkStatus();
  }

  void configureCustomRouterFilter(::envoy::extensions::filters::http::router::v3::Router&) {}

  std::vector<::envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>
      hcm_http_filters_;
  absl::optional<::envoy::config::route::v3::RouteConfiguration> route_configuration_;
  std::vector<::envoy::config::cluster::v3::Cluster> clusters_;
};

} // namespace Platform
} // namespace Envoy
