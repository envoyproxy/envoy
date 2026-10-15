#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

using ::Envoy::Extensions::Matching::Http::DynamicModules::DynamicModuleMatchData;
using ::Envoy::Matcher::MatchResult;

DynamicModuleInputMatcher::DynamicModuleInputMatcher(DynamicModuleSharedPtr module,
                                                     OnMatcherMatchType on_match,
                                                     std::shared_ptr<const void> in_module_config,
                                                     MatchResult on_error_result)
    : module_(std::move(module)), on_match_(on_match),
      in_module_config_(std::move(in_module_config)), on_error_result_(on_error_result) {}

MatchResult DynamicModuleInputMatcher::match(const ::Envoy::Matcher::DataInputGetResult& input) {
  if (auto dynamic_module_data = input.customData<DynamicModuleMatchData>(); dynamic_module_data) {
    // Build the match context with header pointers from the matching data.
    MatchContext context;
    context.request_headers = dynamic_module_data->request_headers_;
    context.response_headers = dynamic_module_data->response_headers_;
    context.response_trailers = dynamic_module_data->response_trailers_;

    const bool matched = on_match_(in_module_config_.get(), static_cast<void*>(&context));
    // The panic barrier reports an evaluation it could not complete through module_error. Apply the
    // configured on_error policy in that case rather than trusting the fail value it returned.
    if (context.module_error) {
      return on_error_result_;
    }
    if (matched) {
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
