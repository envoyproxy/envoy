#include "extensions/filters/http/jwt_authn/filter_config.h"

#include <algorithm> // std::sort

#include "common/common/empty_string.h"

using envoy::extensions::filters::http::jwt_authn::v3::RequirementRule;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

void FilterConfigImpl::init() {
  ENVOY_LOG(debug, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());

  // Note: `this` and `context` have a a lifetime of the listener.
  // That may be shorter of the tls callback if the listener is torn shortly after it is created.
  // We use a shared pointer to make sure this object outlives the tls callbacks.
  auto shared_this = shared_from_this();
  tls_->set([shared_this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalCache>(shared_this->proto_config_, shared_this->time_source_,
                                              shared_this->api_);
  });

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
      rule_pairs_.emplace_back(Matcher::create(rule),
                               Verifier::create(rule.requires(), proto_config_.providers(), *this));
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
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  if (proto_config_.has_filter_state_rules()) {
    filter_state_name_ = proto_config_.filter_state_rules().name();
    for (const auto& it : proto_config_.filter_state_rules().requires()) {
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
