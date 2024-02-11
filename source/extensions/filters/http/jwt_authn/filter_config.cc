#include "source/extensions/filters/http/jwt_authn/filter_config.h"

#include <algorithm> // std::sort

#include "source/common/common/empty_string.h"

using envoy::extensions::filters::http::jwt_authn::v3::RequirementRule;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

FilterConfigImpl::FilterConfigImpl(
    envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
    : proto_config_(std::move(proto_config)), stats_(generateStats(stats_prefix, context.scope())),
      cm_(context.serverFactoryContext().clusterManager()),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {

  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  jwks_cache_ = JwksCache::create(proto_config_, context, Common::JwksFetcher::create, stats_);

  std::vector<std::string> names;
  for (const auto& it : proto_config_.requirement_map()) {
    names.push_back(it.first);
    name_verifiers_.emplace(it.first,
                            Verifier::create(it.second, proto_config_.providers(), *this));
  }
  // sort is just for unit-test since protobuf map order is not deterministic.
  std::sort(names.begin(), names.end());
  all_requirement_names_ = absl::StrJoin(names, ",");

  for (const auto& rule : proto_config_.rules()) {
    switch (rule.requirement_type_case()) {
    case RequirementRule::RequirementTypeCase::kRequires:
      rule_pairs_.emplace_back(
          Matcher::create(rule),
          Verifier::create(rule.requires_(), proto_config_.providers(), *this));
      break;
    case RequirementRule::RequirementTypeCase::kRequirementName: {
      // Use requirement_name to lookup requirement_map.
      auto map_it = proto_config_.requirement_map().find(rule.requirement_name());
      if (map_it == proto_config_.requirement_map().end()) {
        throw EnvoyException(fmt::format("Wrong requirement_name: {}. It should be one of [{}]",
                                         rule.requirement_name(), all_requirement_names_));
      }
      rule_pairs_.emplace_back(Matcher::create(rule),
                               Verifier::create(map_it->second, proto_config_.providers(), *this));
    } break;
    case RequirementRule::RequirementTypeCase::REQUIREMENT_TYPE_NOT_SET:
      rule_pairs_.emplace_back(Matcher::create(rule), nullptr);
      break;
    }
  }

  if (proto_config_.has_filter_state_rules()) {
    filter_state_name_ = proto_config_.filter_state_rules().name();
    for (const auto& it : proto_config_.filter_state_rules().requires_()) {
      filter_state_verifiers_.emplace(
          it.first, Verifier::create(it.second, proto_config_.providers(), *this));
    }
  }
}

std::pair<const Verifier*, std::string>
FilterConfigImpl::findPerRouteVerifier(const PerRouteFilterConfig& per_route) const {
  if (per_route.config().disabled()) {
    return std::make_pair(nullptr, EMPTY_STRING);
  }

  const auto& it = name_verifiers_.find(per_route.config().requirement_name());
  if (it != name_verifiers_.end()) {
    return std::make_pair(it->second.get(), EMPTY_STRING);
  }

  return std::make_pair(
      nullptr, absl::StrCat("Wrong requirement_name: ", per_route.config().requirement_name(),
                            ". It should be one of [", all_requirement_names_, "]"));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
