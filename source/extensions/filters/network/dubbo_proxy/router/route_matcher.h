#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"
#include "common/http/header_utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/router/route.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class RouteEntryImplBase : public RouteEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase>,
                           public Logger::Loggable<Logger::Id::dubbo> {
public:
  RouteEntryImplBase(const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route);
  ~RouteEntryImplBase() override = default;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }

  // Router::Route
  const RouteEntry* routeEntry() const override;

  virtual RouteConstSharedPtr matches(const MessageMetadata& metadata,
                                      uint64_t random_value) const PURE;

protected:
  RouteConstSharedPtr clusterEntry(uint64_t random_value) const;
  bool headersMatch(const Http::HeaderMap& headers) const;

private:
  class WeightedClusterEntry : public RouteEntry, public Route {
  public:
    using WeightedCluster = envoy::config::route::v3::WeightedCluster::ClusterWeight;
    WeightedClusterEntry(const RouteEntryImplBase& parent, const WeightedCluster& cluster);

    uint64_t clusterWeight() const { return cluster_weight_; }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      return metadata_match_criteria_ ? metadata_match_criteria_.get()
                                      : parent_.metadataMatchCriteria();
    }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
    const uint64_t cluster_weight_;
    Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  };

  using WeightedClusterEntrySharedPtr = std::shared_ptr<WeightedClusterEntry>;

  uint64_t total_cluster_weight_;
  const std::string cluster_name_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;

  // TODO(gengleilei) Implement it.
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
};

using RouteEntryImplBaseConstSharedPtr = std::shared_ptr<const RouteEntryImplBase>;

class ParameterRouteEntryImpl : public RouteEntryImplBase {
public:
  ParameterRouteEntryImpl(const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route);
  ~ParameterRouteEntryImpl() override;

  struct ParameterData {
    using ParameterMatchSpecifier =
        envoy::extensions::filters::network::dubbo_proxy::v3::MethodMatch::ParameterMatchSpecifier;
    ParameterData(uint32_t index, const ParameterMatchSpecifier& config);

    Http::HeaderUtility::HeaderMatchType match_type_;
    std::string value_;
    envoy::type::v3::Int64Range range_;
    uint32_t index_;
  };

  // RoutEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  bool matchParameter(absl::string_view request_data, const ParameterData& config_data) const;

  std::vector<ParameterData> parameter_data_list_;
};

class MethodRouteEntryImpl : public RouteEntryImplBase {
public:
  MethodRouteEntryImpl(const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route);
  ~MethodRouteEntryImpl() override;

  // RoutEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  const Matchers::StringMatcherImpl method_name_;
  std::shared_ptr<ParameterRouteEntryImpl> parameter_route_;
};

class SingleRouteMatcherImpl : public RouteMatcher, public Logger::Loggable<Logger::Id::dubbo> {
public:
  using RouteConfig = envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration;
  SingleRouteMatcherImpl(const RouteConfig& config, Server::Configuration::FactoryContext& context);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const override;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  const std::string service_name_;
  const absl::optional<std::string> group_;
  const absl::optional<std::string> version_;
};

class MultiRouteMatcher : public RouteMatcher, public Logger::Loggable<Logger::Id::dubbo> {
public:
  using RouteConfigList = Envoy::Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration>;
  MultiRouteMatcher(const RouteConfigList& route_config_list,
                    Server::Configuration::FactoryContext& context);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const override;

private:
  std::vector<RouteMatcherPtr> route_matcher_list_;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
