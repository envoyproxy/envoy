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
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
    : proto_config_(std::move(proto_config)),
      stats_(generateStats(stats_prefix, proto_config_.stat_prefix(), context.scope())),
      cm_(context.serverFactoryContext().clusterManager()),
      time_source_(context.serverFactoryContext().mainThreadDispatcher().timeSource()) {

  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  jwks_cache_ = JwksCache::create(proto_config_, context, Common::JwksFetcher::create, stats_);

  // Validate provider URIs.
  // Note that the PGV well-known regex for URI is not implemented in C++, otherwise we could add a
  // PGV rule instead of doing this check manually.
  for (const auto& provider_pair : proto_config_.providers()) {
    const auto provider_value = std::get<1>(provider_pair);
    if (provider_value.has_remote_jwks()) {
      absl::string_view provider_uri = provider_value.remote_jwks().http_uri().uri();
      Http::Utility::Url url;
      if (!url.initialize(provider_uri, /*is_connect=*/false)) {
        throw EnvoyException(fmt::format("Provider '{}' has an invalid URI: '{}'",
                                         std::get<0>(provider_pair), provider_uri));
      }
    }
  }

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
          Matcher::create(rule, context.serverFactoryContext()),
          Verifier::create(rule.requires_(), proto_config_.providers(), *this));
      break;
    case RequirementRule::RequirementTypeCase::kRequirementName: {
      // Use requirement_name to lookup requirement_map.
      auto map_it = proto_config_.requirement_map().find(rule.requirement_name());
      if (map_it == proto_config_.requirement_map().end()) {
        throw EnvoyException(fmt::format("Wrong requirement_name: {}. It should be one of [{}]",
                                         rule.requirement_name(), all_requirement_names_));
      }
      rule_pairs_.emplace_back(Matcher::create(rule, context.serverFactoryContext()),
                               Verifier::create(map_it->second, proto_config_.providers(), *this));
    } break;
    case RequirementRule::RequirementTypeCase::REQUIREMENT_TYPE_NOT_SET:
      rule_pairs_.emplace_back(Matcher::create(rule, context.serverFactoryContext()), nullptr);
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


void FilterConfigImpl::validateExtractOnlyWithoutValidationUsage(
    const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config) {
  // Resolve verification status header and detect extract_only usage.
  std::string verification_header = "x-jwt-signature-verified";
  bool extract_only_used = false;
  bool verification_disabled = false;

  for (const auto& rule : config.rules()) {
    if (rule.has_requires() && rule.requires().has_extract_only_without_validation()) {
      extract_only_used = true;
      const auto& econfig = rule.requires().extract_only_without_validation();
      if (!econfig.verification_status_header().empty()) {
        if (econfig.verification_status_header() == "-") {
          verification_disabled = true;
        } else {
          verification_header = econfig.verification_status_header();
        }
      }
      break;
    }
  }

  if (!extract_only_used) {
    return;
  }

  if (verification_disabled) {
    ENVOY_LOG(critical,
              "jwt_authn: SECURITY WARNING - extract_only_without_validation is in use "
              "with verification_status_header DISABLED. Downstream filters have NO way "
              "to distinguish forged JWT claims from verified ones.");
  }

  // Warn per claim header with specific RBAC guidance.
  for (const auto& [provider_name, provider] : config.providers()) {
    for (const auto& claim_to_header : provider.claim_to_headers()) {
      if (verification_disabled) {
        ENVOY_LOG(critical,
                  "jwt_authn: Claim header '{}' (claim: '{}') from provider '{}' "
                  "will be set WITHOUT signature verification and WITHOUT a verification "
                  "status header. Any RBAC or ext_authz policy matching on '{}' can be "
                  "bypassed with a forged JWT.",
                  claim_to_header.header_name(), claim_to_header.claim_name(),
                  provider_name, claim_to_header.header_name());
      } else {
        ENVOY_LOG(warn,
                  "jwt_authn: Claim header '{}' (claim: '{}') from provider '{}' will "
                  "be set WITHOUT signature verification. Ensure RBAC policies check "
                  "'{}' before trusting '{}' for authorization.",
                  claim_to_header.header_name(), claim_to_header.claim_name(),
                  provider_name, verification_header, claim_to_header.header_name());
      }
    }

    if (!provider.forward_payload_header().empty()) {
      ENVOY_LOG(warn,
                "jwt_authn: Payload header '{}' from provider '{}' will contain an "
                "UNVERIFIED JWT payload. Check '{}' before trusting it.",
                provider.forward_payload_header(), provider_name, verification_header);
    }
  }
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
