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
using ::Envoy::Http::LowerCaseString;

struct MutationTestCase {
  std::string name;
  CheckOperation op;
  std::string header_name;
  std::string header_value;
  CheckResult default_result;
  CheckResult allow_routing_result;
  CheckResult allow_envoy_result;
  CheckResult no_system_result;
  CheckResult special_result;
};

class EmptyRulesTest : public testing::TestWithParam<MutationTestCase> {
public:
  Regex::GoogleReEngine regex_engine_;
};

TEST_P(EmptyRulesTest, TestDefaultConfig) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  Checker checker(rules, regex_engine_);
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      test_case.default_result);
}

TEST_P(EmptyRulesTest, TestDisallowAll) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  Checker checker(rules, regex_engine_);
  // With this config, no headers are allowed
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      CheckResult::IGNORE);
}

TEST_P(EmptyRulesTest, TestDisallowAllAndFail) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(true);
  Checker checker(rules, regex_engine_);
  // With this config, no headers are allowed
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      CheckResult::FAIL);
}

TEST_P(EmptyRulesTest, TestAllowRouting) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_allow_all_routing()->set_value(true);
  Checker checker(rules, regex_engine_);
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      test_case.allow_routing_result);
}

TEST_P(EmptyRulesTest, TestNoSystem) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_system()->set_value(true);
  Checker checker(rules, regex_engine_);
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      test_case.no_system_result);
}

TEST_P(EmptyRulesTest, TestAllowEnvoy) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_allow_envoy()->set_value(true);
  Checker checker(rules, regex_engine_);
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      test_case.allow_envoy_result);
}

TEST_P(EmptyRulesTest, TestSpecial) {
  const auto& test_case = GetParam();
  HeaderMutationRules rules;
  rules.mutable_disallow_expression()->mutable_google_re2();
  rules.mutable_disallow_expression()->set_regex("^x-special-one$");
  rules.mutable_allow_expression()->mutable_google_re2();
  rules.mutable_allow_expression()->set_regex("^x-special-two$");
  Checker checker(rules, regex_engine_);
  EXPECT_EQ(
      checker.check(test_case.op, LowerCaseString(test_case.header_name), test_case.header_value),
      test_case.special_result);
}

INSTANTIATE_TEST_SUITE_P(
    EmptyRulesTestSuite, EmptyRulesTest,
    testing::ValuesIn<MutationTestCase>({
        // These four are "routing" headers and are treated differently.
        {"host", CheckOperation::SET, "host", "foo:123", CheckResult::IGNORE, CheckResult::OK,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"authority", CheckOperation::SET, ":authority", "bar:123", CheckResult::IGNORE,
         CheckResult::OK, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"scheme", CheckOperation::SET, ":scheme", "http", CheckResult::IGNORE, CheckResult::OK,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"method", CheckOperation::SET, ":method", "PATCH", CheckResult::IGNORE, CheckResult::OK,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        // Ensure we reject invalid values for certain of these headers.
        {"invalid_host", CheckOperation::SET, "host", "This is not valid!", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"invalid_authority", CheckOperation::SET, ":authority", "This is not valid!",
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE,
         CheckResult::IGNORE},
        {"invalid_scheme", CheckOperation::SET, ":scheme", "fancy rpc scheme", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        // ":path" is a system header but not a routing header.
        {"path", CheckOperation::SET, ":path", "/cats", CheckResult::OK, CheckResult::OK,
         CheckResult::OK, CheckResult::IGNORE, CheckResult::OK},
        // content-type is a normal header that may be modified unless
        // specifically disallowed.
        {"content_type", CheckOperation::SET, "content-type", "application/foo", CheckResult::OK,
         CheckResult::OK, CheckResult::OK, CheckResult::OK, CheckResult::OK},
        // ":status" is usually settable but it must be valid
        {"status", CheckOperation::SET, ":status", "201", CheckResult::OK, CheckResult::OK,
         CheckResult::OK, CheckResult::IGNORE, CheckResult::OK},
        {"status_invalid", CheckOperation::SET, ":status", "Not a number", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"status_range", CheckOperation::SET, ":status", "1", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        // x-envoy headers are treated specially and disallowed unless
        // specifically allowed.
        {"envoy_foo", CheckOperation::SET, "x-envoy-foo", "Bar", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::OK, CheckResult::IGNORE, CheckResult::IGNORE},
        // There are rules in the "TestSpecial" test case that use regexes
        // to control whether these headers may be modified.
        {"special_one", CheckOperation::SET, "x-special-one", "Yay", CheckResult::OK,
         CheckResult::OK, CheckResult::OK, CheckResult::OK, CheckResult::IGNORE},
        {"special_two", CheckOperation::SET, "x-special-two", "Awesome", CheckResult::OK,
         CheckResult::OK, CheckResult::OK, CheckResult::OK, CheckResult::OK},
        // Doesn't really matter what you do but you can't remove certain headers.
        {"remove_system", CheckOperation::REMOVE, ":method", "", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        {"remove_host", CheckOperation::REMOVE, "host", "", CheckResult::IGNORE,
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE},
        // In general, headers can't contain invalid characters.
        {"invalid_value", CheckOperation::SET, "x-my-header", "Not\1a\2valid\3header\4",
         CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE, CheckResult::IGNORE,
         CheckResult::IGNORE},
    }),
    [](const testing::TestParamInfo<EmptyRulesTest::ParamType>& info) { return info.param.name; });

} // namespace
} // namespace MutationRules
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
