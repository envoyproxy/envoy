#include "source/common/matcher/actions/string_returning_action.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stream_info/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Matcher {
namespace Actions {

class StringReturningActionTest : public testing::Test {
public:
  void setConfig(std::string yaml) {
    TestUtility::loadFromYaml(yaml, typed_config_);
    factory_ = Config::Utility::getAndCheckFactory<
        Matcher::ActionFactory<StringReturningActionFactoryContext>>(typed_config_);
    config_ = factory_->createEmptyConfigProto();
    typed_config_.typed_config().UnpackTo(config_.get());
    action_ = factory_->createAction(*config_, factory_context_,
                                     ProtobufMessage::getStrictValidationVisitor());
    ASSERT_NE(nullptr, action_);
  }
  const StringReturningAction& action() {
    EXPECT_NE(nullptr, action_);
    return action_->getTyped<StringReturningAction>();
  }

protected:
  xds::core::v3::TypedExtensionConfig typed_config_;
  ProtobufTypes::MessagePtr config_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  StringReturningActionFactoryContext factory_context_{server_factory_context_};
  OptRef<Matcher::ActionFactory<StringReturningActionFactoryContext>> factory_;
  Matcher::ActionConstSharedPtr action_;
};

TEST_F(StringReturningActionTest, SubstitutionFormatStringReturnsDynamicValue) {
  setConfig(R"EOF(
    name: test_dynamic_metadata
    typed_config:
      "@type": "type.googleapis.com/envoy.config.core.v3.SubstitutionFormatString"
      text_format_source:
        inline_string: "%DYNAMIC_METADATA(com.test_filter:test_key)%"
)EOF");
  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  {
    std::string result = action().getOutputString(info);
    EXPECT_EQ("-", result);
  }

  {
    const std::string metadata_string = R"EOF(
  filter_metadata:
    com.test_filter:
      test_key: foo
)EOF";
    TestUtility::loadFromYaml(metadata_string, info.metadata_);
    std::string result = action().getOutputString(info);
    EXPECT_EQ("foo", result);
  }
}

TEST_F(StringReturningActionTest, SubstitutionFormatStringInvalidConfigThrows) {
  EXPECT_THROW_WITH_REGEX(setConfig(R"EOF(
    name: test_typo_in_dynamic_metadata
    typed_config:
      "@type": "type.googleapis.com/envoy.config.core.v3.SubstitutionFormatString"
      text_format_source:
        inline_string: "%DYNABIC_METADATA(com.test_filter:test_key)%"
)EOF"),
                          EnvoyException, "DYNABIC_METADATA");
}

TEST_F(StringReturningActionTest, StringValueReturnsCorrectValue) {
  setConfig(R"EOF(
    name: test_string_value
    typed_config:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
      value: "foo"
)EOF");
  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  std::string result = action().getOutputString(info);
  EXPECT_EQ("foo", result);
}

TEST_F(StringReturningActionTest, UnsetStringValueReturnsEmptyString) {
  setConfig(R"EOF(
    name: test_empty_string_value
    typed_config:
      "@type": "type.googleapis.com/google.protobuf.StringValue"
)EOF");
  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  std::string result = action().getOutputString(info);
  EXPECT_EQ("", result);
}

} // namespace Actions
} // namespace Matcher
} // namespace Envoy
