#include "extensions/filters/network/dubbo_proxy/router/route_matcher.h"

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/dubbo_proxy/serializer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route)
    : cluster_name_(route.route().cluster()),
      config_headers_(Http::HeaderUtility::buildHeaderDataVector(route.match().headers())) {
  if (route.route().cluster_specifier_case() ==
      envoy::extensions::filters::network::dubbo_proxy::v3::RouteAction::ClusterSpecifierCase::
          kWeightedClusters) {
    total_cluster_weight_ = 0UL;
    for (const auto& cluster : route.route().weighted_clusters().clusters()) {
      weighted_clusters_.emplace_back(std::make_shared<WeightedClusterEntry>(*this, cluster));
      total_cluster_weight_ += weighted_clusters_.back()->clusterWeight();
    }
    ENVOY_LOG(debug, "dubbo route matcher: weighted_clusters_size {}", weighted_clusters_.size());
  }
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(uint64_t random_value) const {
  if (weighted_clusters_.empty()) {
    ENVOY_LOG(debug, "dubbo route matcher: weighted_clusters_size {}", weighted_clusters_.size());
    return shared_from_this();
  }

  return WeightedClusterUtil::pickCluster(weighted_clusters_, total_cluster_weight_, random_value,
                                          false);
}

bool RouteEntryImplBase::headersMatch(const Http::HeaderMap& headers) const {
  ENVOY_LOG(debug, "dubbo route matcher: headers size {}, metadata headers size {}",
            config_headers_.size(), headers.size());
  return Http::HeaderUtility::matchHeaders(headers, config_headers_);
}

RouteEntryImplBase::WeightedClusterEntry::WeightedClusterEntry(const RouteEntryImplBase& parent,
                                                               const WeightedCluster& cluster)
    : parent_(parent), cluster_name_(cluster.name()),
      cluster_weight_(PROTOBUF_GET_WRAPPED_REQUIRED(cluster, weight)) {}

ParameterRouteEntryImpl::ParameterRouteEntryImpl(
    const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route)
    : RouteEntryImplBase(route) {
  for (auto& config : route.match().method().params_match()) {
    parameter_data_list_.emplace_back(config.first, config.second);
  }
}

ParameterRouteEntryImpl::~ParameterRouteEntryImpl() = default;

bool ParameterRouteEntryImpl::matchParameter(absl::string_view request_data,
                                             const ParameterData& config_data) const {
  switch (config_data.match_type_) {
  case Http::HeaderUtility::HeaderMatchType::Value:
    return config_data.value_.empty() || request_data == config_data.value_;
  case Http::HeaderUtility::HeaderMatchType::Range: {
    int64_t value = 0;
    return absl::SimpleAtoi(request_data, &value) && value >= config_data.range_.start() &&
           value < config_data.range_.end();
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

RouteConstSharedPtr ParameterRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                     uint64_t random_value) const {
  ASSERT(metadata.hasInvocationInfo());
  const auto invocation = dynamic_cast<const RpcInvocationImpl*>(&metadata.invocationInfo());
  ASSERT(invocation);
  if (!invocation->hasParameters()) {
    return nullptr;
  }

  ENVOY_LOG(debug, "dubbo route matcher: parameter name match");
  for (auto& config_data : parameter_data_list_) {
    const std::string& data = invocation->getParameterValue(config_data.index_);
    if (data.empty()) {
      ENVOY_LOG(debug,
                "dubbo route matcher: parameter matching failed, there are no parameters in the "
                "user request, index '{}'",
                config_data.index_);
      return nullptr;
    }

    if (!matchParameter(data, config_data)) {
      ENVOY_LOG(debug, "dubbo route matcher: parameter matching failed, index '{}', value '{}'",
                config_data.index_, data);
      return nullptr;
    }
  }

  return clusterEntry(random_value);
}

ParameterRouteEntryImpl::ParameterData::ParameterData(uint32_t index,
                                                      const ParameterMatchSpecifier& config) {
  index_ = index;
  switch (config.parameter_match_specifier_case()) {
  case ParameterMatchSpecifier::kExactMatch:
    match_type_ = Http::HeaderUtility::HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case ParameterMatchSpecifier::kRangeMatch:
    match_type_ = Http::HeaderUtility::HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  default:
    match_type_ = Http::HeaderUtility::HeaderMatchType::Value;
    break;
  }
}

MethodRouteEntryImpl::MethodRouteEntryImpl(
    const envoy::extensions::filters::network::dubbo_proxy::v3::Route& route)
    : RouteEntryImplBase(route), method_name_(route.match().method().name()) {
  if (route.match().method().params_match_size() != 0) {
    parameter_route_ = std::make_shared<ParameterRouteEntryImpl>(route);
  }
}

MethodRouteEntryImpl::~MethodRouteEntryImpl() = default;

RouteConstSharedPtr MethodRouteEntryImpl::matches(const MessageMetadata& metadata,
                                                  uint64_t random_value) const {
  ASSERT(metadata.hasInvocationInfo());
  const auto invocation = dynamic_cast<const RpcInvocationImpl*>(&metadata.invocationInfo());
  ASSERT(invocation);

  if (invocation->hasHeaders() && !RouteEntryImplBase::headersMatch(invocation->headers())) {
    ENVOY_LOG(error, "dubbo route matcher: headers not match");
    return nullptr;
  }

  if (invocation->methodName().empty()) {
    ENVOY_LOG(error, "dubbo route matcher: there is no method name in the metadata");
    return nullptr;
  }

  if (!method_name_.match(invocation->methodName())) {
    ENVOY_LOG(debug, "dubbo route matcher: method matching failed, input method '{}'",
              invocation->methodName());
    return nullptr;
  }

  if (parameter_route_) {
    ENVOY_LOG(debug, "dubbo route matcher: parameter matching is required");
    return parameter_route_->matches(metadata, random_value);
  }

  return clusterEntry(random_value);
}

SingleRouteMatcherImpl::SingleRouteMatcherImpl(const RouteConfig& config,
                                               Server::Configuration::FactoryContext&)
    : service_name_(config.interface()), group_(config.group()), version_(config.version()) {
  using envoy::extensions::filters::network::dubbo_proxy::v3::RouteMatch;

  for (const auto& route : config.routes()) {
    routes_.emplace_back(std::make_shared<MethodRouteEntryImpl>(route));
  }
  ENVOY_LOG(debug, "dubbo route matcher: routes list size {}", routes_.size());
}

RouteConstSharedPtr SingleRouteMatcherImpl::route(const MessageMetadata& metadata,
                                                  uint64_t random_value) const {
  ASSERT(metadata.hasInvocationInfo());
  const auto& invocation = metadata.invocationInfo();

  if (service_name_ == invocation.serviceName() &&
      (group_.value().empty() ||
       (invocation.serviceGroup().has_value() && invocation.serviceGroup().value() == group_)) &&
      (version_.value().empty() || (invocation.serviceVersion().has_value() &&
                                    invocation.serviceVersion().value() == version_))) {
    for (const auto& route : routes_) {
      RouteConstSharedPtr route_entry = route->matches(metadata, random_value);
      if (nullptr != route_entry) {
        return route_entry;
      }
    }
  } else {
    ENVOY_LOG(debug, "dubbo route matcher: interface matching failed");
  }

  return nullptr;
}

MultiRouteMatcher::MultiRouteMatcher(const RouteConfigList& route_config_list,
                                     Server::Configuration::FactoryContext& context) {
  for (const auto& route_config : route_config_list) {
    route_matcher_list_.emplace_back(
        std::make_unique<SingleRouteMatcherImpl>(route_config, context));
  }
  ENVOY_LOG(debug, "route matcher list size {}", route_matcher_list_.size());
}

RouteConstSharedPtr MultiRouteMatcher::route(const MessageMetadata& metadata,
                                             uint64_t random_value) const {
  for (const auto& route_matcher : route_matcher_list_) {
    auto route = route_matcher->route(metadata, random_value);
    if (nullptr != route) {
      return route;
    }
  }

  return nullptr;
}

class DefaultRouteMatcherConfigFactory : public RouteMatcherFactoryBase<MultiRouteMatcher> {
public:
  DefaultRouteMatcherConfigFactory() : RouteMatcherFactoryBase(RouteMatcherType::Default) {}
};

/**
 * Static registration for the Dubbo protocol. @see RegisterFactory.
 */
REGISTER_FACTORY(DefaultRouteMatcherConfigFactory, NamedRouteMatcherConfigFactory);

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
