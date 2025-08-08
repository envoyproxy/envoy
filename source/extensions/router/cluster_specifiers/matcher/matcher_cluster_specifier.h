#pragma once

#include "envoy/extensions/router/cluster_specifiers/matcher/v3/matcher.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace Matcher {

using MatcherClusterSpecifierConfigProto =
    envoy::extensions::router::cluster_specifiers::matcher::v3::MatcherClusterSpecifier;
using ClusterActionProto =
    envoy::extensions::router::cluster_specifiers::matcher::v3::ClusterAction;

/**
 * ClusterActionContext is used to construct ClusterAction. Empty struct because ClusterAction
 * doesn't need any context.
 */
struct ClusterActionContext {};

/**
 * ClusterAction is used to store the matched cluster name.
 */
class ClusterAction : public Envoy::Matcher::ActionBase<ClusterActionProto> {
public:
  explicit ClusterAction(absl::string_view cluster) : cluster_(cluster) {}

  const std::string& cluster() const { return cluster_; }

private:
  const std::string cluster_;
};

// Registered factory for ClusterAction. This factory will be used to load proto configuration
// from opaque config.
class ClusterActionFactory : public Envoy::Matcher::ActionFactory<ClusterActionContext> {
public:
  Envoy::Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ClusterActionContext& context,
               ProtobufMessage::ValidationVisitor& validation_visitor) override;
  std::string name() const override { return "cluster"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ClusterActionProto>();
  }
};
DECLARE_FACTORY(ClusterActionFactory);

/**
 * MatcherClusterSpecifierPlugin is the specific cluster specifier plugin. It will get the
 * target cluster name from the matched cluster action.
 */
class MatcherClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                      Logger::Loggable<Logger::Id::router> {
public:
  MatcherClusterSpecifierPlugin(
      Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree)
      : match_tree_(match_tree) {}
  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t) const override;

private:
  Envoy::Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> match_tree_;
};

} // namespace Matcher
} // namespace Router
} // namespace Extensions
} // namespace Envoy
