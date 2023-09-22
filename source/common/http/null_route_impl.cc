#include "null_route_impl.h"

#include <vector>

namespace Envoy {
namespace Http {
const std::vector<std::reference_wrapper<const Router::RateLimitPolicyEntry>>
    NullRateLimitPolicy::rate_limit_policy_entry_;
const NullHedgePolicy RouteEntryImpl::hedge_policy_;
const NullRateLimitPolicy RouteEntryImpl::rate_limit_policy_;
const Router::InternalRedirectPolicyImpl RouteEntryImpl::internal_redirect_policy_;
const Router::PathMatcherSharedPtr RouteEntryImpl::path_matcher_;
const Router::PathRewriterSharedPtr RouteEntryImpl::path_rewriter_;
const std::vector<Router::ShadowPolicyPtr> RouteEntryImpl::shadow_policies_;
const NullVirtualHost RouteEntryImpl::virtual_host_;
const NullRateLimitPolicy NullVirtualHost::rate_limit_policy_;
const NullCommonConfig NullVirtualHost::route_configuration_;
const std::multimap<std::string, std::string> RouteEntryImpl::opaque_config_;
const NullPathMatchCriterion RouteEntryImpl::path_match_criterion_;
const RouteEntryImpl::ConnectConfigOptRef RouteEntryImpl::connect_config_nullopt_;
const std::list<LowerCaseString> NullCommonConfig::internal_only_headers_;
} // namespace Http
} // namespace Envoy
