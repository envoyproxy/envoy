#include "source/extensions/filters/http/jwt_authn/filter_config.h"

#include <algorithm> // std::sort

#include "source/common/common/empty_string.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

using envoy::extensions::filters::http::jwt_authn::v3::RequirementRule;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

FilterConfigImpl::FilterConfigImpl(
    envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context,
    absl::Status& creation_status)
    : proto_config_(std::move(proto_config)),
      stats_(generateStats(stats_prefix, proto_config_.stat_prefix(), context.scope())),
      cm_(context.serverFactoryContext().clusterManager()),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {

  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  auto jwks_cache_or =
      JwksCache::create(proto_config_, context, Common::JwksFetcher::create, stats_);
  SET_AND_RETURN_IF_NOT_OK(jwks_cache_or.status(), creation_status);
  jwks_cache_ = std::move(jwks_cache_or.value());

  // Validate provider URIs.
  // Note that the PGV well-known regex for URI is not implemented in C++, otherwise we could add a
  // PGV rule instead of doing this check manually.
  for (const auto& provider_pair : proto_config_.providers()) {
    const auto provider_value = std::get<1>(provider_pair);
    if (provider_value.has_remote_jwks()) {
      absl::string_view provider_uri = provider_value.remote_jwks().http_uri().uri();
      Http::Utility::Url url;
      if (!url.initialize(provider_uri, /*is_connect=*/false)) {
        creation_status = absl::InvalidArgumentError(fmt::format(
            "Provider '{}' has an invalid URI: '{}'", std::get<0>(provider_pair), provider_uri));
        return;
      }
    }
  }

  std::vector<std::string> names;
  for (const auto& it : proto_config_.requirement_map()) {
    names.push_back(it.first);
    auto verifier_or = Verifier::create(it.second, proto_config_.providers(), *this);
    SET_AND_RETURN_IF_NOT_OK(verifier_or.status(), creation_status);
    name_verifiers_.emplace(it.first, std::move(verifier_or.value()));
  }
  // sort is just for unit-test since protobuf map order is not deterministic.
  std::sort(names.begin(), names.end());
  all_requirement_names_ = absl::StrJoin(names, ",");

  for (const auto& rule : proto_config_.rules()) {
    switch (rule.requirement_type_case()) {
    case RequirementRule::RequirementTypeCase::kRequires: {
      auto matcher_or = Matcher::create(rule, context.serverFactoryContext());
      SET_AND_RETURN_IF_NOT_OK(matcher_or.status(), creation_status);
      auto verifier_or = Verifier::create(rule.requires_(), proto_config_.providers(), *this);
      SET_AND_RETURN_IF_NOT_OK(verifier_or.status(), creation_status);
      rule_pairs_.emplace_back(std::move(matcher_or.value()), std::move(verifier_or.value()));
      break;
    }
    case RequirementRule::RequirementTypeCase::kRequirementName: {
      // Use requirement_name to lookup requirement_map.
      auto map_it = proto_config_.requirement_map().find(rule.requirement_name());
      if (map_it == proto_config_.requirement_map().end()) {
        creation_status = absl::InvalidArgumentError(
            fmt::format("Wrong requirement_name: {}. It should be one of [{}]",
                        rule.requirement_name(), all_requirement_names_));
        return;
      }
      auto matcher_or = Matcher::create(rule, context.serverFactoryContext());
      SET_AND_RETURN_IF_NOT_OK(matcher_or.status(), creation_status);
      auto verifier_or = Verifier::create(map_it->second, proto_config_.providers(), *this);
      SET_AND_RETURN_IF_NOT_OK(verifier_or.status(), creation_status);
      rule_pairs_.emplace_back(std::move(matcher_or.value()), std::move(verifier_or.value()));
    } break;
    case RequirementRule::RequirementTypeCase::REQUIREMENT_TYPE_NOT_SET: {
      auto matcher_or = Matcher::create(rule, context.serverFactoryContext());
      SET_AND_RETURN_IF_NOT_OK(matcher_or.status(), creation_status);
      rule_pairs_.emplace_back(std::move(matcher_or.value()), nullptr);
      break;
    }
    }
  }

  if (proto_config_.has_filter_state_rules()) {
    filter_state_name_ = proto_config_.filter_state_rules().name();
    for (const auto& it : proto_config_.filter_state_rules().requires_()) {
      auto verifier_or = Verifier::create(it.second, proto_config_.providers(), *this);
      SET_AND_RETURN_IF_NOT_OK(verifier_or.status(), creation_status);
      filter_state_verifiers_.emplace(it.first, std::move(verifier_or.value()));
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
