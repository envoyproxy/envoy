#include <memory>

#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/extensions/common/matcher/trie_matcher.h"

#include "test/extensions/common/matcher/custom_matcher_test.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {
namespace {

using ::Envoy::Matcher::ActionFactory;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::StringAction;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;
using ::Envoy::Matcher::TestDataInputFactory;

class TrieMatcherTest : public CustomMatcherTest<TrieMatcherFactoryBase<TestData>> {};

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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputFactory("input", "192.101.0.1");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "128.0.0.1");
    validateNoMatch();
  }
  {
    auto input = TestDataInputFactory("input", "xxx");
    validateNoMatch();
  }
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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    validateMatch("foo");
  }
  {
    // No range matches.
    auto input = TestDataInputFactory("input", "128.0.0.1");
    validateMatch("bar");
  }
  {
    // Input is not a valid IP.
    auto input = TestDataInputFactory("input", "xxx");
    validateMatch("bar");
  }
  {
    // Input is nullopt.
    auto input = TestDataInputFactory(
        "input", {DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt});
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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputFactory("input", "192.0.0.1");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "255.0.0.1");
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
                  "@type": type.googleapis.com/google.protobuf.StringValue
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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "baz");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "");
    validateMatch("foo");
  }
  {
    auto input = TestDataInputFactory("input", "128.0.0.1");
    auto nested = TestDataInputFactory("nested", "");
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
                  "@type": type.googleapis.com/google.protobuf.StringValue
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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "baz");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "");
    validateNoMatch();
  }
  {
    auto input = TestDataInputFactory("input", "128.0.0.1");
    auto nested = TestDataInputFactory("nested", "");
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
                  "@type": type.googleapis.com/google.protobuf.StringValue
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
                      "@type": type.googleapis.com/google.protobuf.StringValue
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
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "baz");
    validateMatch("baz");
  }
  {
    auto input = TestDataInputFactory("input", "192.0.100.1");
    auto nested = TestDataInputFactory("nested", "bar");
    validateMatch("bar");
  }
  {
    auto input = TestDataInputFactory("input", "128.0.0.1");
    auto nested = TestDataInputFactory("nested", "");
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
  )EOF";
  loadConfig(yaml);

  {
    auto input = TestDataInputFactory(
        "input", {DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt});
    auto nested = TestDataInputFactory("nested", "");
    validateNoMatch();
  }
  {
    auto input = TestDataInputFactory("input", "127.0.0.1");
    auto nested = TestDataInputFactory(
        "nested", {DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt});
    validateNoMatch();
  }
  {
    auto input = TestDataInputFactory(
        "input", {DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt});
    auto nested = TestDataInputFactory("nested", "");
    validateUnableToMatch();
  }
  {
    auto input = TestDataInputFactory("input", "127.0.0.1");
    auto nested = TestDataInputFactory(
        "nested", {DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt});
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
  Network::Matching::MatchingDataImpl data(socket);

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
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("192.168.0.1", 8080);
  Network::Matching::UdpMatchingDataImpl data(address, address);

  const auto result = match_tree()->match(data);
  EXPECT_EQ(result.match_state_, MatchState::MatchComplete);
  EXPECT_EQ(result.on_match_->action_cb_()->getTyped<StringAction>().string_, "foo");
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
