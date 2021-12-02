#include <string>

#include "source/extensions/filters/common/mutation_rules/mutation_rules.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace MutationRules {
namespace {

using ::envoy::config::common::mutation_rules::v3::HeaderMutationRules;

struct MutationTestCase {
  std::string name;
  std::string header_name;
  CheckResult default_result;
  CheckResult allow_routing_result;
  CheckResult allow_envoy_result;
  CheckResult no_system_result;
  CheckResult special_result;
};

using EmptyRulesTest = testing::TestWithParam<MutationTestCase>;

TEST_P(EmptyRulesTest, TestDefaultConfig) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  Checker checker(rules);
  EXPECT_EQ(checker.check(test_case.header_name), test_case.default_result);
}

TEST_P(EmptyRulesTest, TestDisallowAll) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  Checker checker(rules);
  // With this config, no headers are allowed
  EXPECT_EQ(checker.check(test_case.header_name), CheckResult::IGNORE);
}

TEST_P(EmptyRulesTest, TestDisallowAllAndFail) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(true);
  Checker checker(rules);
  // With this config, no headers are allowed
  EXPECT_EQ(checker.check(test_case.header_name), CheckResult::FAIL);
}

TEST_P(EmptyRulesTest, TestAllowRouting) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_allow_all_routing()->set_value(true);
  Checker checker(rules);
  EXPECT_EQ(checker.check(test_case.header_name), test_case.allow_routing_result);
}

TEST_P(EmptyRulesTest, TestNoSystem) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_system()->set_value(true);
  Checker checker(rules);
  EXPECT_EQ(checker.check(test_case.header_name), test_case.no_system_result);
}

TEST_P(EmptyRulesTest, TestAllowEnvoy) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_allow_envoy()->set_value(true);
  Checker checker(rules);
  EXPECT_EQ(checker.check(test_case.header_name), test_case.allow_envoy_result);
}

TEST_P(EmptyRulesTest, TestSpecial) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_is_error()->set_value(true);
  rules.mutable_disallow_expression()->mutable_google_re2();
  rules.mutable_disallow_expression()->set_regex("^x-special-one$");
  rules.mutable_allow_expression()->mutable_google_re2();
  rules.mutable_allow_expression()->set_regex("^x-special-two$");
  Checker checker(rules);
  EXPECT_EQ(checker.check(test_case.header_name), test_case.special_result);
}

INSTANTIATE_TEST_SUITE_P(EmptyRulesTestSuite, EmptyRulesTest,
                         testing::ValuesIn<MutationTestCase>({
                             // These four are "routing" headers and are treated differently.
                             {"host", "host", CheckResult::IGNORE, CheckResult::OK,
                              CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::FAIL},
                             {"authority", ":authority", CheckResult::IGNORE, CheckResult::OK,
                              CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::FAIL},
                             {"scheme", ":scheme", CheckResult::IGNORE, CheckResult::OK,
                              CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::FAIL},
                             {"method", ":method", CheckResult::IGNORE, CheckResult::OK,
                              CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::FAIL},
                             // ":path" is a system header but not a routing header.
                             {"path", ":path", CheckResult::OK, CheckResult::OK, CheckResult::OK,
                              CheckResult::IGNORE, CheckResult::OK},
                             // content-type is a normal header that may be modified unless
                             // specifically disallowed.
                             {"content_type", "content-type", CheckResult::OK, CheckResult::OK,
                              CheckResult::OK, CheckResult::OK, CheckResult::OK},
                             // x-envoy headers are treated specially and disallowed unless
                             // specifically allowed.
                             {"envoy_foo", "x-envoy-foo", CheckResult::IGNORE, CheckResult::IGNORE,
                              CheckResult::OK, CheckResult::IGNORE, CheckResult::FAIL},
                             // There are rules in the "TestSpecial" test case that use regexes
                             // to control whether these headers may be modified.
                             {"special_one", "x-special-one", CheckResult::OK, CheckResult::OK,
                              CheckResult::OK, CheckResult::OK, CheckResult::FAIL},
                             {"special_two", "x-special-two", CheckResult::OK, CheckResult::OK,
                              CheckResult::OK, CheckResult::OK, CheckResult::OK},
                         }),
                         [](const testing::TestParamInfo<EmptyRulesTest::ParamType>& info) {
                           return info.param.name;
                         });

} // namespace
} // namespace MutationRules
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy