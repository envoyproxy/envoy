#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/dubbo_proxy.pb.h"
#include "envoy/config/filter/network/dubbo_proxy/v2alpha1/route.pb.h"

#include "common/common/logger.h"
#include "common/http/header_utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class Utility {
public:
  static bool isContainWildcard(const std::string& input);
  static bool wildcardMatch(const char* input, const char* pattern);
};

class RouteEntryImplBase : public RouteEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase>,
                           public Logger::Loggable<Logger::Id::dubbo> {
public:
  RouteEntryImplBase(const envoy::config::filter::network::dubbo_proxy::v2alpha1::Route& route);
  virtual ~RouteEntryImplBase() {}

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
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
    using WeightedCluster =
        envoy::config::filter::network::dubbo_proxy::v2alpha1::WeightedCluster_ClusterWeight;
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

  typedef std::shared_ptr<WeightedClusterEntry> WeightedClusterEntrySharedPtr;

  uint64_t total_cluster_weight_;
  const std::string cluster_name_;
  std::vector<Http::HeaderUtility::HeaderData> config_headers_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;

  // TODO(leilei.gll) Implement it.
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
};

typedef std::shared_ptr<const RouteEntryImplBase> RouteEntryImplBaseConstSharedPtr;

class ParamterRouteEntryImpl : public RouteEntryImplBase {
public:
  ParamterRouteEntryImpl(const envoy::config::filter::network::dubbo_proxy::v2alpha1::Route& route);
  ~ParamterRouteEntryImpl() override;

  struct ParamterData {
    using ParamterConfig =
        envoy::config::filter::network::dubbo_proxy::v2alpha1::MethodMatch_Parameter;
    ParamterData(const ParamterConfig& config);

    Http::HeaderUtility::HeaderMatchType match_type_;
    std::string value_;
    envoy::type::Int64Range range_;
    uint32_t index_;
    std::string type_;
  };

  // RoutEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  bool matchParameter(const std::string& request_data, const ParamterData& config_data) const;

  const std::string method_name_;
  std::vector<ParamterData> paramter_data_list_;
};

class MethodRouteEntryImpl : public RouteEntryImplBase {
public:
  MethodRouteEntryImpl(const envoy::config::filter::network::dubbo_proxy::v2alpha1::Route& route);
  ~MethodRouteEntryImpl() override;

  const std::string& methodName() const { return method_name_; }

  // RoutEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  const std::string method_name_;
  bool is_contain_wildcard_;
  absl::optional<std::shared_ptr<ParamterRouteEntryImpl>> paramter_route_;
};

class RouteMatcher : public Logger::Loggable<Logger::Id::dubbo> {
public:
  using RouteConfig = envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration;
  RouteMatcher(const RouteConfig& config);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  const std::string interface_name_;
  const absl::optional<std::string> group_;
  const absl::optional<std::string> version_;
};

typedef std::shared_ptr<const RouteMatcher> RouteMatcherConstSharedPtr;
typedef std::unique_ptr<RouteMatcher> RouteMatcherPtr;

class MultiRouteMatcher : public Logger::Loggable<Logger::Id::dubbo> {
public:
  using RouteConfigList = Envoy::Protobuf::RepeatedPtrField<
      ::envoy::config::filter::network::dubbo_proxy::v2alpha1::RouteConfiguration>;
  MultiRouteMatcher(const RouteConfigList& route_config_list);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const;

private:
  std::vector<RouteMatcherPtr> route_matcher_list_;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
