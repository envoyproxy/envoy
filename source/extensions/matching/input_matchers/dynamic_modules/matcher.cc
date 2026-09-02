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
                                                     std::shared_ptr<const void> in_module_config)
    : module_(std::move(module)), on_match_(on_match),
      in_module_config_(std::move(in_module_config)) {}

MatchResult DynamicModuleInputMatcher::match(const ::Envoy::Matcher::DataInputGetResult& input) {
  if (auto dynamic_module_data = input.customData<DynamicModuleMatchData>(); dynamic_module_data) {
    // Build the match context with header pointers from the matching data.
    MatchContext context;
    context.request_headers = dynamic_module_data->request_headers_;
    context.response_headers = dynamic_module_data->response_headers_;
    context.response_trailers = dynamic_module_data->response_trailers_;

    if (on_match_(in_module_config_.get(), static_cast<void*>(&context))) {
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
