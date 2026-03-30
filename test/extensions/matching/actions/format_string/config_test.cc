#include "source/extensions/matching/actions/format_string/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/stream_info/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

TEST(ConfigTest, TestFilterChainConfig) {
  const std::string yaml_string = R"EOF(
    text_format_source:
      inline_string: "%DYNAMIC_METADATA(com.test_filter:test_key)%"
)EOF";

  xds::core::v3::TypedExtensionConfig typed_config;
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml_string, config);
  typed_config.mutable_typed_config()->PackFrom(config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  auto& factory =
      Config::Utility::getAndCheckFactory<Matcher::ActionFactory<FilterChainActionFactoryContext>>(
          typed_config);
  auto action =
      factory.createAction(config, factory_context, ProtobufMessage::getStrictValidationVisitor());
  ASSERT_NE(nullptr, action);
  const auto& typed_action = action->getTyped<Server::Configuration::FilterChainBaseAction>();

  Server::Configuration::FilterChainsByName chains;
  auto chain = std::make_shared<testing::NiceMock<Network::MockFilterChain>>();
  chains.emplace("foo", chain);

  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  {
    auto result = typed_action.get(chains, info);
    EXPECT_EQ(nullptr, result);
  }

  {
    const std::string metadata_string = R"EOF(
  filter_metadata:
    com.test_filter:
      test_key: foo
)EOF";
    TestUtility::loadFromYaml(metadata_string, info.metadata_);
    auto result = typed_action.get(chains, info);
    ASSERT_NE(nullptr, result);
    ASSERT_EQ(chain.get(), result);
  }
}

TEST(ConfigTest, TestStringReturningConfig) {
  const std::string yaml_string = R"EOF(
    text_format_source:
      inline_string: "%DYNAMIC_METADATA(com.test_filter:test_key)%"
)EOF";

  xds::core::v3::TypedExtensionConfig typed_config;
  envoy::config::core::v3::SubstitutionFormatString config;
  TestUtility::loadFromYaml(yaml_string, config);
  typed_config.mutable_typed_config()->PackFrom(config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  StringReturningActionFactoryContext factory_context{server_factory_context};
  auto& factory = Config::Utility::getAndCheckFactory<
      Matcher::ActionFactory<StringReturningActionFactoryContext>>(typed_config);
  auto action =
      factory.createAction(config, factory_context, ProtobufMessage::getStrictValidationVisitor());
  ASSERT_NE(nullptr, action);
  const auto& typed_action = action->getTyped<StringReturningAction>();

  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  {
    auto result = typed_action.string(info);
    EXPECT_EQ("-", result);
  }

  {
    const std::string metadata_string = R"EOF(
  filter_metadata:
    com.test_filter:
      test_key: foo
)EOF";
    TestUtility::loadFromYaml(metadata_string, info.metadata_);
    auto result = typed_action.string(info);
    EXPECT_EQ("foo", result);
  }
}

TEST(ConfigTest, TestDirectStringReturningConfig) {
  const std::string yaml_string = R"EOF(
    value: "foo"
)EOF";

  xds::core::v3::TypedExtensionConfig typed_config;
  Protobuf::StringValue config;
  TestUtility::loadFromYaml(yaml_string, config);
  typed_config.mutable_typed_config()->PackFrom(config);
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context;
  StringReturningActionFactoryContext factory_context{server_factory_context};
  auto& factory = Config::Utility::getAndCheckFactory<
      Matcher::ActionFactory<StringReturningActionFactoryContext>>(typed_config);
  auto action =
      factory.createAction(config, factory_context, ProtobufMessage::getStrictValidationVisitor());
  ASSERT_NE(nullptr, action);
  const auto& typed_action = action->getTyped<StringReturningAction>();

  testing::NiceMock<StreamInfo::MockStreamInfo> info;
  auto result = typed_action.string(info);
  EXPECT_EQ("foo", result);
}

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
