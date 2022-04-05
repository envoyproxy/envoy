#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

using ::Envoy::Matcher::ActionFactory;
using ::Envoy::Matcher::CustomMatcherFactory;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchState;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::StringAction;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;

template <class CustomMatcherFactoryBase> class CustomMatcherTest : public ::testing::Test {
public:
  CustomMatcherTest()
      : inject_action_(action_factory_), inject_matcher_(matcher_factory_),
        factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void loadConfig(const std::string& config) {
    MessageUtil::loadFromYaml(config, matcher_, ProtobufMessage::getStrictValidationVisitor());
    TestUtility::validate(matcher_);
  }
  void validateMatch(const std::string& output) {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_TRUE(result.on_match_.has_value());
    EXPECT_NE(result.on_match_->action_cb_, nullptr);
    auto action = result.on_match_->action_cb_();
    const auto value = action->getTyped<StringAction>();
    EXPECT_EQ(value.string_, output);
  }
  void validateNoMatch() {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
    EXPECT_FALSE(result.on_match_.has_value());
  }
  void validateUnableToMatch() {
    auto match_tree = factory_.create(matcher_);
    const auto result = match_tree()->match(TestData());
    EXPECT_EQ(result.match_state_, MatchState::UnableToMatch);
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  CustomMatcherFactoryBase matcher_factory_;
  Registry::InjectFactory<CustomMatcherFactory<TestData>> inject_matcher_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
  xds::type::matcher::v3::Matcher matcher_;
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
