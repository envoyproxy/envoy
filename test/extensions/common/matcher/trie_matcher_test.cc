#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/matcher/trie_matcher.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "xds/type/matcher/v3/matcher.pb.h"
#include "xds/type/matcher/v3/matcher.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {
namespace {

using ::Envoy::Matcher::ActionFactory;
using ::Envoy::Matcher::CustomMatcherFactory;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::StringAction;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;
using ::Envoy::Matcher::TestDataInputBoolFactory;
using ::Envoy::Matcher::TestDataInputStringFactory;

class TrieMatcherTest : public ::testing::Test {
public:
  TrieMatcherTest()
      : inject_action_(action_factory_), inject_matcher_(trie_matcher_factory_),
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
  TrieMatcherFactoryBase<TestData> trie_matcher_factory_;
  Registry::InjectFactory<CustomMatcherFactory<TestData>> inject_matcher_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
  xds::type::matcher::v3::Matcher matcher_;
};

TEST_F(TrieMatcherTest, TestMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 192.101.0.0
          prefix_len: 10
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputStringFactory("192.101.0.1");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    validateNoMatch();
  }
  {
    auto input = TestDataInputStringFactory("xxx");
    validateNoMatch();
  }
}

TEST_F(TrieMatcherTest, TestInvalidMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.FloatValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 192.101.0.0
          prefix_len: 10
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
  )EOF";
  loadConfig(yaml);
  auto input_factory = ::Envoy::Matcher::TestDataInputFloatFactory(3.14);
  auto match_tree = factory_.create(matcher_);
  std::string error_message = absl::StrCat("Unsupported data input type: float, currently only "
                                           "string type is supported in trie matcher");
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(TrieMatcherTest, TestMatcherOnNoMatch) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
on_no_match:
  action:
    name: bar
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    validateMatch("foo");
  }
  {
    // No range matches.
    auto input = TestDataInputStringFactory("128.0.0.1");
    validateMatch("bar");
  }
  {
    // Input is not a valid IP.
    auto input = TestDataInputStringFactory("xxx");
    validateMatch("bar");
  }
  {
    // Input is nullopt.
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    validateMatch("bar");
  }
}

TEST_F(TrieMatcherTest, OverlappingMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 128.0.0.0
          prefix_len: 1
        - address_prefix: 192.0.0.0
          prefix_len: 2
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 255.0.0.0
          prefix_len: 8
        - address_prefix: 192.0.0.0
          prefix_len: 2
        - address_prefix: 192.0.0.1
          prefix_len: 32
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputStringFactory("192.0.0.1");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputStringFactory("255.0.0.1");
    validateMatch("bar");
  }
}

TEST_F(TrieMatcherTest, NestedInclusiveMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 0.0.0.0
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  baz:
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("baz");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    validateMatch("foo");
  }
}

TEST_F(TrieMatcherTest, NestedExclusiveMatcher) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 0.0.0.0
        exclusive: true
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        exclusive: true
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  baz:
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: bar
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("baz");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("");
    validateNoMatch();
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    validateMatch("foo");
  }
}

TEST_F(TrieMatcherTest, RecursiveMatcherTree) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 0.0.0.0
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  bar:
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: bar
            on_no_match:
              matcher:
                matcher_tree:
                  input:
                    name: nested
                    typed_config:
                      "@type": type.googleapis.com/google.protobuf.BoolValue
                  exact_match_map:
                    map:
                      baz:
                        action:
                          name: test_action
                          typed_config:
                            "@type": type.googleapis.com/google.protobuf.StringValue
                            value: baz
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("baz");
    validateMatch("baz");
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("bar");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    validateMatch("foo");
  }
}

TEST_F(TrieMatcherTest, NoData) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 0.0.0.0
        on_match:
          matcher:
            matcher_tree:
              input:
                name: nested
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              custom_match:
                name: ip_matcher
                typed_config:
                  "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
                  range_matchers:
                  - ranges:
                    - address_prefix: 192.0.0.0
                      prefix_len: 2
                    on_match:
                      action:
                        name: test_action
                        typed_config:
                          "@type": type.googleapis.com/google.protobuf.StringValue
                          value: foo
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    auto nested = TestDataInputBoolFactory("");
    validateNoMatch();
  }
  {
    auto input = TestDataInputStringFactory("127.0.0.1");
    auto nested = TestDataInputBoolFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    validateNoMatch();
  }
  {
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()});
    auto nested = TestDataInputBoolFactory("");
    validateUnableToMatch();
  }
  {
    auto input = TestDataInputStringFactory("127.0.0.1");
    auto nested = TestDataInputBoolFactory(
        {DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()});
    validateUnableToMatch();
  }
}

TEST(TrieMatcherIntegrationTest, NetworkMatchingData) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )EOF";
  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  StringActionFactory action_factory;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action(action_factory);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  MockMatchTreeValidationVisitor<Network::MatchingData> validation_visitor;
  EXPECT_CALL(validation_visitor, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  absl::string_view context = "";
  MatchTreeFactory<Network::MatchingData, absl::string_view> matcher_factory(
      context, factory_context, validation_visitor);
  auto match_tree = matcher_factory.create(matcher);

  Network::MockConnectionSocket socket;
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("192.168.0.1", 8080));
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  Network::Matching::MatchingDataImpl data(socket, filter_state, metadata);

  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_->action_cb_()->getTyped<StringAction>().string_, "foo");
}

TEST(TrieMatcherIntegrationTest, UdpNetworkMatchingData) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )EOF";
  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  StringActionFactory action_factory;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action(action_factory);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  MockMatchTreeValidationVisitor<Network::UdpMatchingData> validation_visitor;
  EXPECT_CALL(validation_visitor, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  absl::string_view context = "";
  MatchTreeFactory<Network::UdpMatchingData, absl::string_view> matcher_factory(
      context, factory_context, validation_visitor);
  auto match_tree = matcher_factory.create(matcher);

  Network::MockConnectionSocket socket;
  const Network::Address::Ipv4Instance address("192.168.0.1", 8080);
  Network::Matching::UdpMatchingDataImpl data(address, address);

  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_->action_cb_()->getTyped<StringAction>().string_, "foo");
}

TEST(TrieMatcherIntegrationTest, HttpMatchingData) {
  const std::string yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput
  custom_match:
    name: ip_matcher
    typed_config:
      "@type": type.googleapis.com/xds.type.matcher.v3.IPMatcher
      range_matchers:
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
  )EOF";
  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(yaml, matcher, ProtobufMessage::getStrictValidationVisitor());

  StringActionFactory action_factory;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action(action_factory);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  MockMatchTreeValidationVisitor<Http::HttpMatchingData> validation_visitor;
  EXPECT_CALL(validation_visitor, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  absl::string_view context = "";
  MatchTreeFactory<Http::HttpMatchingData, absl::string_view> matcher_factory(
      context, factory_context, validation_visitor);
  auto match_tree = matcher_factory.create(matcher);

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("192.168.0.1", 8080);
  stream_info.downstream_connection_info_provider_->setLocalAddress(address);
  stream_info.downstream_connection_info_provider_->setRemoteAddress(address);

  Http::Matching::HttpMatchingDataImpl data(stream_info);

  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_->action_cb_()->getTyped<StringAction>().string_, "foo");
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
