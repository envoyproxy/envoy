#include "source/extensions/router/cluster_specifiers/matcher/matcher_cluster_specifier.h"

#include "envoy/extensions/router/cluster_specifiers/matcher/v3/matcher.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

Envoy::Matcher::ActionConstSharedPtr
ClusterActionFactory::createAction(const Protobuf::Message& config, ClusterActionContext&,
                                   ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ClusterActionProto&>(config, validation_visitor);
  return std::make_shared<ClusterAction>(proto_config.cluster());
}

REGISTER_FACTORY(ClusterActionFactory, Envoy::Matcher::ActionFactory<ClusterActionContext>);

class MatcherRouteEntry : public Envoy::Router::DelegatingRouteEntry {
public:
  MatcherRouteEntry(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                    Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree)
      : DelegatingRouteEntry(std::move(parent)), match_tree_(std::move(match_tree)) {}

  const std::string& clusterName() const override {
    return cluster_name_.has_value() ? *cluster_name_ : EMPTY_STRING;
  }

  void refreshRouteCluster(const Http::RequestHeaderMap& headers,
                           const StreamInfo::StreamInfo& stream_info) const override {
    Http::Matching::HttpMatchingDataImpl data(stream_info);
    data.onRequestHeaders(headers);

    Envoy::Matcher::MatchResult match_result =
        Envoy::Matcher::evaluateMatch<Http::HttpMatchingData>(*match_tree_, data);

    if (!match_result.isMatch()) {
      return;
    }
    cluster_name_.emplace(match_result.action()->getTyped<ClusterAction>().cluster());
  }

private:
  Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_;
  mutable OptRef<const std::string> cluster_name_;
};

Envoy::Router::RouteConstSharedPtr
MatcherClusterSpecifierPlugin::route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                     const Http::RequestHeaderMap& headers,
                                     const StreamInfo::StreamInfo& stream_info, uint64_t) const {
  auto matcher_route = std::make_shared<MatcherRouteEntry>(parent, match_tree_);
  matcher_route->refreshRouteCluster(headers, stream_info);
  return matcher_route;
}

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
