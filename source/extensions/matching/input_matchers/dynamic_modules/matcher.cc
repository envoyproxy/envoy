#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

using ::Envoy::Extensions::Matching::Http::DynamicModules::DynamicModuleMatchData;
using ::Envoy::Matcher::MatchResult;

DynamicModuleInputMatcher::DynamicModuleInputMatcher(
    DynamicModuleSharedPtr module, OnMatcherConfigDestroyType on_config_destroy,
    OnMatcherMatchType on_match,
    envoy_dynamic_module_type_matcher_config_module_ptr in_module_config)
    : module_(std::move(module)), on_config_destroy_(on_config_destroy), on_match_(on_match),
      in_module_config_(in_module_config) {}

DynamicModuleInputMatcher::~DynamicModuleInputMatcher() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

MatchResult DynamicModuleInputMatcher::match(const ::Envoy::Matcher::MatchingDataType& input) {
  if (auto* ptr = absl::get_if<std::shared_ptr<::Envoy::Matcher::CustomMatchData>>(&input);
      ptr != nullptr) {
    auto* dynamic_module_data = dynamic_cast<DynamicModuleMatchData*>((*ptr).get());
    if (dynamic_module_data == nullptr) {
      ENVOY_LOG(debug, "dynamic module matcher received non-DynamicModuleMatchData input");
      return MatchResult::NoMatch;
    }

    // Build the match context with header pointers from the matching data.
    MatchContext context;
    context.request_headers = dynamic_module_data->request_headers_;
    context.response_headers = dynamic_module_data->response_headers_;
    context.response_trailers = dynamic_module_data->response_trailers_;

    if (on_match_(in_module_config_, static_cast<void*>(&context))) {
      return MatchResult::Matched;
    }
  }

  return MatchResult::NoMatch;
}

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
