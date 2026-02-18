#include "source/extensions/matching/input_matchers/dynamic_modules/matcher.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace DynamicModules {
namespace {

class DynamicModuleMatcherTest : public testing::Test {
public:
  DynamicModuleMatcherTest() {
    std::string shared_object_path =
        Extensions::DynamicModules::testSharedObjectPath("matcher_no_op", "c");
    std::string shared_object_dir =
        std::filesystem::path(shared_object_path).parent_path().string();
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", shared_object_dir, 1);
  }
};

TEST_F(DynamicModuleMatcherTest, AlwaysMatchModule) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_no_op", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  ASSERT_TRUE(on_config_new.ok());

  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  ASSERT_TRUE(on_config_destroy.ok());

  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");
  ASSERT_TRUE(on_match.ok());

  envoy_dynamic_module_type_envoy_buffer name_buf = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"", 0};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);
  ASSERT_NE(nullptr, in_module_config);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  // Create matching data with DynamicModuleMatchData.
  auto match_data = std::make_shared<Http::DynamicModules::DynamicModuleMatchData>();
  ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-test", "value"}};
  match_data->request_headers_ = &request_headers;

  ::Envoy::Matcher::MatchingDataType input =
      std::shared_ptr<::Envoy::Matcher::CustomMatchData>(match_data);

  // The no-op matcher always returns true.
  EXPECT_TRUE(matcher->match(input));
}

TEST_F(DynamicModuleMatcherTest, HeaderCheckModule) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_check_headers", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  ASSERT_TRUE(on_config_new.ok());

  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  ASSERT_TRUE(on_config_destroy.ok());

  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");
  ASSERT_TRUE(on_match.ok());

  // Configure the module to look for "x-test-header".
  envoy_dynamic_module_type_envoy_buffer name_buf = {"header_check", 12};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"x-test-header", 13};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);
  ASSERT_NE(nullptr, in_module_config);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  // Test with matching header.
  {
    auto match_data = std::make_shared<Http::DynamicModules::DynamicModuleMatchData>();
    ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-test-header", "match"}};
    match_data->request_headers_ = &request_headers;

    ::Envoy::Matcher::MatchingDataType input =
        std::shared_ptr<::Envoy::Matcher::CustomMatchData>(match_data);

    EXPECT_TRUE(matcher->match(input));
  }

  // Test with non-matching value.
  {
    auto match_data = std::make_shared<Http::DynamicModules::DynamicModuleMatchData>();
    ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"x-test-header", "no-match"}};
    match_data->request_headers_ = &request_headers;

    ::Envoy::Matcher::MatchingDataType input =
        std::shared_ptr<::Envoy::Matcher::CustomMatchData>(match_data);

    EXPECT_FALSE(matcher->match(input));
  }

  // Test with missing header.
  {
    auto match_data = std::make_shared<Http::DynamicModules::DynamicModuleMatchData>();
    ::Envoy::Http::TestRequestHeaderMapImpl request_headers{{"other-header", "match"}};
    match_data->request_headers_ = &request_headers;

    ::Envoy::Matcher::MatchingDataType input =
        std::shared_ptr<::Envoy::Matcher::CustomMatchData>(match_data);

    EXPECT_FALSE(matcher->match(input));
  }
}

TEST_F(DynamicModuleMatcherTest, SupportedDataInputTypes) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_no_op", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");

  envoy_dynamic_module_type_envoy_buffer name_buf = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"", 0};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  auto types = matcher->supportedDataInputTypes();
  EXPECT_EQ(1, types.size());
  EXPECT_TRUE(types.contains("dynamic_module_data_input"));
}

// A non-DynamicModuleMatchData CustomMatchData implementation for testing.
class OtherCustomMatchData : public ::Envoy::Matcher::CustomMatchData {};

TEST_F(DynamicModuleMatcherTest, NonDynamicModuleCustomMatchDataReturnsFalse) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_no_op", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");

  envoy_dynamic_module_type_envoy_buffer name_buf = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"", 0};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  // Pass a CustomMatchData that is not DynamicModuleMatchData.
  auto other_data = std::make_shared<OtherCustomMatchData>();
  ::Envoy::Matcher::MatchingDataType input =
      std::shared_ptr<::Envoy::Matcher::CustomMatchData>(other_data);

  EXPECT_FALSE(matcher->match(input));
}

TEST_F(DynamicModuleMatcherTest, NonCustomMatchDataReturnsFalse) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_no_op", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");

  envoy_dynamic_module_type_envoy_buffer name_buf = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"", 0};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  // Pass a string variant instead of CustomMatchData.
  ::Envoy::Matcher::MatchingDataType input = std::string("not_custom_data");
  EXPECT_FALSE(matcher->match(input));
}

TEST_F(DynamicModuleMatcherTest, NullRequestHeaders) {
  auto module_or_error =
      Extensions::DynamicModules::newDynamicModuleByName("matcher_check_headers", true, false);
  ASSERT_TRUE(module_or_error.ok());

  auto module = std::shared_ptr<Extensions::DynamicModules::DynamicModule>(
      std::move(module_or_error.value()));

  auto on_config_new = module->getFunctionPointer<OnMatcherConfigNewType>(
      "envoy_dynamic_module_on_matcher_config_new");
  auto on_config_destroy = module->getFunctionPointer<OnMatcherConfigDestroyType>(
      "envoy_dynamic_module_on_matcher_config_destroy");
  auto on_match =
      module->getFunctionPointer<OnMatcherMatchType>("envoy_dynamic_module_on_matcher_match");

  envoy_dynamic_module_type_envoy_buffer name_buf = {"test", 4};
  envoy_dynamic_module_type_envoy_buffer config_buf = {"x-test", 6};
  auto in_module_config = (*on_config_new.value())(nullptr, name_buf, config_buf);

  auto matcher = std::make_unique<DynamicModuleInputMatcher>(module, on_config_destroy.value(),
                                                             on_match.value(), in_module_config);

  // Create match data with no headers set.
  auto match_data = std::make_shared<Http::DynamicModules::DynamicModuleMatchData>();

  ::Envoy::Matcher::MatchingDataType input =
      std::shared_ptr<::Envoy::Matcher::CustomMatchData>(match_data);

  EXPECT_FALSE(matcher->match(input));
}

} // namespace
} // namespace DynamicModules
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
