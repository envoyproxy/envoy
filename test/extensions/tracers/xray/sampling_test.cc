#include <vector>

#include "extensions/tracers/xray/sampling.h"

#include "test/test_common/test_time.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
TEST(XRayLocalizedSamplingStrategyTest, defaultRules) {
  DangerousDeprecatedTestTime test_time;
  LocalizedSamplingStrategy localized_sampling_strategy("", test_time.timeSystem());
  std::vector<SamplingRule> rules = localized_sampling_strategy.defaultRuleManifest().rules();

  EXPECT_EQ(0, rules.size());
  EXPECT_EQ("", localized_sampling_strategy.defaultRuleManifest().defaultRule().serviceName());

  EXPECT_EQ("", localized_sampling_strategy.defaultRuleManifest().defaultRule().httpMethod());

  EXPECT_EQ("", localized_sampling_strategy.defaultRuleManifest().defaultRule().urlPath());
  EXPECT_EQ(1, localized_sampling_strategy.defaultRuleManifest().defaultRule().fixedTarget());
  EXPECT_FLOAT_EQ(0.05, localized_sampling_strategy.defaultRuleManifest().defaultRule().rate());
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
