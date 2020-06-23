#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/route.pb.h"
#include "envoy/server/filter_config.h"

#include "common/common/logger.h"
#include "common/common/matchers.h"
#include "common/http/header_utility.h"

#include "extensions/filters/network/rocketmq_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class MessageMetadata;

namespace Router {

class RouteEntryImpl : public RouteEntry,
                       public Route,
                       public std::enable_shared_from_this<RouteEntryImpl>,
                       public Logger::Loggable<Logger::Id::rocketmq> {
public:
  RouteEntryImpl(const envoy::extensions::filters::network::rocketmq_proxy::v3::Route& route);
  ~RouteEntryImpl() override = default;

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }

  // Router::Route
  const RouteEntry* routeEntry() const override;

  RouteConstSharedPtr matches(const MessageMetadata& metadata) const;

private:
  bool headersMatch(const Http::HeaderMap& headers) const;

  const Matchers::StringMatcherImpl topic_name_;
  const std::string cluster_name_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
};

using RouteEntryImplConstSharedPtr = std::shared_ptr<const RouteEntryImpl>;

class RouteMatcher : public Logger::Loggable<Logger::Id::rocketmq> {
public:
  using RouteConfig = envoy::extensions::filters::network::rocketmq_proxy::v3::RouteConfiguration;
  RouteMatcher(const RouteConfig& config);

  RouteConstSharedPtr route(const MessageMetadata& metadata) const;

private:
  std::vector<RouteEntryImplConstSharedPtr> routes_;
};

using RouteMatcherPtr = std::unique_ptr<RouteMatcher>;

} // namespace Router
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy