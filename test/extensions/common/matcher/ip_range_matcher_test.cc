#include <memory>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/registry/registry.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/matcher/ip_range_matcher.h"

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

using ::Envoy::Matcher::ActionConstSharedPtr;
using ::Envoy::Matcher::ActionFactory;
using ::Envoy::Matcher::CustomMatcherFactory;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::HasInsufficientData;
using ::Envoy::Matcher::HasNoMatch;
using ::Envoy::Matcher::HasStringAction;
using ::Envoy::Matcher::IsStringAction;
using ::Envoy::Matcher::MatchResult;
using ::Envoy::Matcher::MatchTreeFactory;
using ::Envoy::Matcher::MatchTreePtr;
using ::Envoy::Matcher::MatchTreeSharedPtr;
using ::Envoy::Matcher::MockMatchTreeValidationVisitor;
using ::Envoy::Matcher::SkippedMatchCb;
using ::Envoy::Matcher::StringActionFactory;
using ::Envoy::Matcher::TestData;
using ::Envoy::Matcher::TestDataInputBoolFactory;
using ::Envoy::Matcher::TestDataInputStringFactory;
using ::testing::ElementsAre;

class IpRangeMatcherTest : public ::testing::Test {
public:
  IpRangeMatcherTest()
      : inject_action_(action_factory_), inject_matcher_(ip_range_matcher_factory_),
        factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void loadConfig(const std::string& config) {
    MessageUtil::loadFromYaml(config, matcher_, ProtobufMessage::getStrictValidationVisitor());
    TestUtility::validate(matcher_);
  }

  MatchResult doMatch() {
    MatchTreePtr<TestData> match_tree = factory_.create(matcher_)();
    return match_tree->match(TestData(), skipped_match_cb_);
  }

  StringActionFactory action_factory_;
  Registry::InjectFactory<ActionFactory<absl::string_view>> inject_action_;
  IpRangeMatcherFactoryBase<TestData> ip_range_matcher_factory_;
  Registry::InjectFactory<CustomMatcherFactory<TestData>> inject_matcher_;
  MockMatchTreeValidationVisitor<TestData> validation_visitor_;

  absl::string_view context_ = "";
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  MatchTreeFactory<TestData, absl::string_view> factory_;
  xds::type::matcher::v3::Matcher matcher_;
  // If expecting keep_matching matchers, set this cb & mark its support in the validation_visitor_.
  SkippedMatchCb skipped_match_cb_ = nullptr;
};

TEST_F(IpRangeMatcherTest, TestMatcher) {
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
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
  {
    auto input = TestDataInputStringFactory("192.101.0.1");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    EXPECT_THAT(doMatch(), HasNoMatch());
  }
  {
    auto input = TestDataInputStringFactory("xxx");
    EXPECT_THAT(doMatch(), HasNoMatch());
  }
}

TEST_F(IpRangeMatcherTest, TestInvalidMatcher) {
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
                                           "string type is supported in IP range matcher");
  EXPECT_THROW_WITH_MESSAGE(match_tree(), EnvoyException, error_message);
}

TEST_F(IpRangeMatcherTest, TestMatcherOnNoMatch) {
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
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
  {
    // No range matches.
    auto input = TestDataInputStringFactory("128.0.0.1");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    // Input is not a valid IP.
    auto input = TestDataInputStringFactory("xxx");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    // Input is nullopt.
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
}

TEST_F(IpRangeMatcherTest, OverlappingMatcher) {
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
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
  {
    auto input = TestDataInputStringFactory("192.0.0.1");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    auto input = TestDataInputStringFactory("255.0.0.1");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
}

TEST_F(IpRangeMatcherTest, NestedInclusiveMatcher) {
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
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
}

TEST_F(IpRangeMatcherTest, NestedExclusiveMatcher) {
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
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasNoMatch());
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
}

TEST_F(IpRangeMatcherTest, RecursiveMatcherTree) {
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
    EXPECT_THAT(doMatch(), HasStringAction("baz"));
  }
  {
    auto input = TestDataInputStringFactory("192.0.100.1");
    auto nested = TestDataInputBoolFactory("bar");
    EXPECT_THAT(doMatch(), HasStringAction("bar"));
  }
  {
    auto input = TestDataInputStringFactory("128.0.0.1");
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasStringAction("foo"));
  }
}

TEST_F(IpRangeMatcherTest, NoData) {
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
    EXPECT_THAT(doMatch(), HasNoMatch());
  }
  {
    auto input = TestDataInputStringFactory("127.0.0.1");
    auto nested = TestDataInputBoolFactory(
        {DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()});
    EXPECT_THAT(doMatch(), HasNoMatch());
  }
  {
    auto input = TestDataInputStringFactory(
        {DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()});
    auto nested = TestDataInputBoolFactory("");
    EXPECT_THAT(doMatch(), HasInsufficientData());
  }
  {
    auto input = TestDataInputStringFactory("127.0.0.1");
    auto nested = TestDataInputBoolFactory(
        {DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()});
    EXPECT_THAT(doMatch(), HasInsufficientData());
  }
}

TEST_F(IpRangeMatcherTest, ExerciseKeepMatching) {
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
          prefix_len: 0
        on_match:
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: bar
      - ranges:
        - address_prefix: 192.0.0.0
          prefix_len: 2
        on_match:
          keep_matching: true
          matcher:
            matcher_tree:
              input:
                name: nested
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.BoolValue
              exact_match_map:
                map:
                  baz:
                    keep_matching: true
                    action:
                      name: test_action
                      typed_config:
                        "@type": type.googleapis.com/google.protobuf.StringValue
                        value: baz
            on_no_match:
              action:
                name: test_action
                typed_config:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: bag
      - ranges:
        - address_prefix: 192.101.0.0
          prefix_len: 10
        on_match:
          keep_matching: true
          action:
            name: test_action
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: foo
on_no_match:
  action:
    name: bat
    typed_config:
      "@type": type.googleapis.com/google.protobuf.StringValue
      value: bat
  )EOF";

  validation_visitor_.setSupportKeepMatching(true);
  loadConfig(yaml);

  // Skip foo because keep_matching is set on the top-level matcher.
  // Skip baz because the nested matcher is set with keep_matching.
  // Skip bag because the nested matcher returns on_no_match, but the top-level matcher is set to
  // keep_matching.
  std::vector<ActionConstSharedPtr> skipped_results{};
  skipped_match_cb_ = [&skipped_results](const ActionConstSharedPtr& cb) {
    skipped_results.push_back(cb);
  };

  auto input = TestDataInputStringFactory("192.101.0.1");
  auto nested = TestDataInputBoolFactory("baz");
  // Matches 192.101.0.0, 192.0.0.0, and 0.0.0.0.
  EXPECT_THAT(doMatch(), HasStringAction("bar"));
  EXPECT_THAT(skipped_results,
              ElementsAre(IsStringAction("foo"), IsStringAction("baz"), IsStringAction("bag")));
}

TEST(IpRangeMatcherIntegrationTest, NetworkMatchingData) {
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

  const MatchResult result = match_tree()->match(data);
  EXPECT_THAT(result, HasStringAction("foo"));
}

TEST(IpRangeMatcherIntegrationTest, UdpNetworkMatchingData) {
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

  const MatchResult result = match_tree()->match(data);
  EXPECT_THAT(result, HasStringAction("foo"));
}

TEST(IpRangeMatcherIntegrationTest, HttpMatchingData) {
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

  const MatchResult result = match_tree()->match(data);
  EXPECT_THAT(result, HasStringAction("foo"));
}

} // namespace
} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
