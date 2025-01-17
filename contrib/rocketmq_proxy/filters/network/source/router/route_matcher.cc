#include "contrib/rocketmq_proxy/filters/network/source/router/route_matcher.h"

#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/rocketmq_proxy/filters/network/source/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {
namespace Router {

RouteEntryImpl::RouteEntryImpl(
    const envoy::extensions::filters::network::rocketmq_proxy::v3::Route& route,
    Server::Configuration::CommonFactoryContext& context)
    : topic_name_(route.match().topic(), context), cluster_name_(route.route().cluster()),
      config_headers_(
          Http::HeaderUtility::buildHeaderDataVector(route.match().headers(), context)) {

  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_ =
          std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }
}

const std::string& RouteEntryImpl::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImpl::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImpl::matches(const MessageMetadata& metadata) const {
  if (headersMatch(metadata.headers())) {
    const std::string& topic_name = metadata.topicName();
    if (topic_name_.match(topic_name)) {
      return shared_from_this();
    }
  }
  return nullptr;
}

bool RouteEntryImpl::headersMatch(const Http::HeaderMap& headers) const {
  ENVOY_LOG(debug, "rocketmq route matcher: headers size {}, metadata headers size {}",
            config_headers_.size(), headers.size());
  return Http::HeaderUtility::matchHeaders(headers, config_headers_);
}

RouteMatcher::RouteMatcher(const RouteConfig& config,
                           Server::Configuration::CommonFactoryContext& context) {
  for (const auto& route : config.routes()) {
    routes_.emplace_back(std::make_shared<RouteEntryImpl>(route, context));
  }
  ENVOY_LOG(debug, "rocketmq route matcher: routes list size {}", routes_.size());
}

RouteConstSharedPtr RouteMatcher::route(const MessageMetadata& metadata) const {
  const std::string& topic_name = metadata.topicName();
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata);
    if (nullptr != route_entry) {
      ENVOY_LOG(debug, "rocketmq route matcher: find cluster success for topic: {}", topic_name);
      return route_entry;
    }
  }
  ENVOY_LOG(debug, "rocketmq route matcher: find cluster failed for topic: {}", topic_name);
  return nullptr;
}

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
