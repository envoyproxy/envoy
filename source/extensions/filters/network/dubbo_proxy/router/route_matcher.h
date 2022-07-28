#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/server/filter_config.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"
#include "source/extensions/filters/network/dubbo_proxy/router/router.h"

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
  bool headersMatch(const RpcInvocationImpl& invocation) const;

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
  const Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> method_name_;
  std::shared_ptr<ParameterRouteEntryImpl> parameter_route_;
};

class SingleRouteMatcherImpl : public Logger::Loggable<Logger::Id::dubbo> {
public:
  class InterfaceMatcher {
  public:
    InterfaceMatcher(const std::string& interface_name);
    bool match(const absl::string_view interface) const { return impl_(interface); }

  private:
    std::function<bool(const absl::string_view)> impl_;
  };

  using RouteConfig = envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration;
  SingleRouteMatcherImpl(const RouteConfig& config,
                         Server::Configuration::ServerFactoryContext& context);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const;

private:
  bool matchServiceName(const RpcInvocationImpl& invocation) const;
  bool matchServiceVersion(const RpcInvocationImpl& invocation) const;
  bool matchServiceGroup(const RpcInvocationImpl& invocation) const;

  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
  const InterfaceMatcher interface_matcher_;
  const absl::optional<std::string> group_;
  const absl::optional<std::string> version_;
};
using SingleRouteMatcherImplPtr = std::unique_ptr<SingleRouteMatcherImpl>;

class RouteConfigImpl : public Config, public Logger::Loggable<Logger::Id::dubbo> {
public:
  using RouteConfigList = Envoy::Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration>;
  using MultipleRouteConfig =
      envoy::extensions::filters::network::dubbo_proxy::v3::MultipleRouteConfiguration;
  RouteConfigImpl(const RouteConfigList& route_config_list,
                  Server::Configuration::ServerFactoryContext& context,
                  bool validate_clusters_default = false);
  RouteConfigImpl(const MultipleRouteConfig& multiple_route_config,
                  Server::Configuration::ServerFactoryContext& context,
                  bool validate_clusters_default = false)
      : RouteConfigImpl(multiple_route_config.route_config(), context, validate_clusters_default) {}

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const override;

private:
  std::vector<SingleRouteMatcherImplPtr> route_matcher_list_;
};
using RouteConfigImplPtr = std::unique_ptr<RouteConfigImpl>;

class NullRouteConfigImpl : public Config {
public:
  RouteConstSharedPtr route(const MessageMetadata&, uint64_t) const override { return nullptr; }
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
