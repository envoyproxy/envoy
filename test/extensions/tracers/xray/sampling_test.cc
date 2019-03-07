#include "extensions/tracers/xray/sampling.h"

#include "test/test_common/test_time.h"
#include "gtest/gtest.h"
#include <vector>

namespace Envoy {
    namespace Extensions {
        namespace Tracers {
            namespace XRay {
                TEST(XRayLocalizedSamplingStrategyTest, defaultRules) {
                    LocalizedSamplingStrategy localized_sampling_strategy = LocalizedSamplingStrategy("");
                    SamplingRule default_rule = localized_sampling_strategy.defaultRuleManifest().defaultRule();
                    std::vector<SamplingRule> rules = localized_sampling_strategy.defaultRuleManifest().rules();

                    EXPECT_EQ(0, rules.size());
                    EXPECT_EQ("", default_rule.serviceName());

                    EXPECT_EQ("", default_rule.httpMethod());

                    EXPECT_EQ("", default_rule.urlPath());
                    EXPECT_EQ(1, default_rule.fixedTarget());
                    EXPECT_FLOAT_EQ(0.05, default_rule.rate());
                }

            } // namespace XRay
        } // namespace Tracers
    } // namespace Extensions
} // namespace Envoy

