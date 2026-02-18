#pragma once

#include <memory>

#include "envoy/matcher/matcher.h"

#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/matching/http/dynamic_modules/data_input.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {

// Type aliases for function pointers resolved from the module.
using OnMatcherConfigNewType = decltype(&envoy_dynamic_module_on_matcher_config_new);
using OnMatcherConfigDestroyType = decltype(&envoy_dynamic_module_on_matcher_config_destroy);
using OnMatcherMatchType = decltype(&envoy_dynamic_module_on_matcher_match);

// Shared ownership of the dynamic module to keep it loaded while any matcher references it.
using DynamicModuleSharedPtr = std::shared_ptr<Extensions::DynamicModules::DynamicModule>;

/**
 * Context passed to the dynamic module during a match evaluation. The matcher_input_envoy_ptr
 * points to this struct, and the module accesses the matching data via ABI callbacks.
 *
 * This struct is only valid during the envoy_dynamic_module_on_matcher_match callback.
 */
struct MatchContext {
  const ::Envoy::Http::RequestHeaderMap* request_headers{};
  const ::Envoy::Http::ResponseHeaderMap* response_headers{};
  const ::Envoy::Http::ResponseTrailerMap* response_trailers{};
};

/**
 * InputMatcher implementation that delegates matching logic to a dynamic module.
 * The module is loaded and configured during construction, and match() calls the
 * module's event hook on each evaluation.
 */
class DynamicModuleInputMatcher : public ::Envoy::Matcher::InputMatcher,
                                  public Logger::Loggable<Logger::Id::matcher> {
public:
  DynamicModuleInputMatcher(DynamicModuleSharedPtr module,
                            OnMatcherConfigDestroyType on_config_destroy,
                            OnMatcherMatchType on_match,
                            envoy_dynamic_module_type_matcher_config_module_ptr in_module_config);

  ~DynamicModuleInputMatcher() override;

  bool match(const ::Envoy::Matcher::MatchingDataType& input) override;

  absl::flat_hash_set<std::string> supportedDataInputTypes() const override {
    return absl::flat_hash_set<std::string>{"dynamic_module_data_input"};
  }

private:
  // Prevent copy/move.
  DynamicModuleInputMatcher(const DynamicModuleInputMatcher&) = delete;
  DynamicModuleInputMatcher& operator=(const DynamicModuleInputMatcher&) = delete;

  DynamicModuleSharedPtr module_;
  OnMatcherConfigDestroyType on_config_destroy_;
  OnMatcherMatchType on_match_;
  envoy_dynamic_module_type_matcher_config_module_ptr in_module_config_;
};

} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
