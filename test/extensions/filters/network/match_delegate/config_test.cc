#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/matching/data_impl.h"
#include "source/extensions/filters/network/match_delegate/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MatchDelegate {
namespace {

// Create a custom MockReadFilter that doesn't set expectations in the constructor
class CustomMockReadFilter : public Network::ReadFilter {
public:
  MOCK_METHOD(Network::FilterStatus, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(Network::FilterStatus, onNewConnection, ());
  MOCK_METHOD(void, initializeReadFilterCallbacks, (Network::ReadFilterCallbacks & callbacks));
};

// Create a custom MockWriteFilter that doesn't set expectations in the constructor
class CustomMockWriteFilter : public Network::WriteFilter {
public:
  MOCK_METHOD(Network::FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (Network::WriteFilterCallbacks & callbacks));
};

// Create a custom MockFilter that doesn't set expectations in the constructor
class CustomMockFilter : public Network::Filter {
public:
  MOCK_METHOD(Network::FilterStatus, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(Network::FilterStatus, onNewConnection, ());
  MOCK_METHOD(Network::FilterStatus, onWrite, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, initializeReadFilterCallbacks, (Network::ReadFilterCallbacks & callbacks));
  MOCK_METHOD(void, initializeWriteFilterCallbacks, (Network::WriteFilterCallbacks & callbacks));
};

struct TestFactory : public Envoy::Server::Configuration::NamedNetworkFilterConfigFactory {
  std::string name() const override { return "test"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }

  absl::StatusOr<Envoy::Network::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    return [](auto& filter_manager) {
      std::shared_ptr<Envoy::Network::ReadFilter> read_filter(new CustomMockReadFilter());
      std::shared_ptr<Envoy::Network::WriteFilter> write_filter(new CustomMockWriteFilter());
      std::shared_ptr<Envoy::Network::Filter> filter(new CustomMockFilter());
      filter_manager.addReadFilter(read_filter);
      filter_manager.addWriteFilter(write_filter);
      filter_manager.addFilter(filter);
    };
  }

  Server::Configuration::MatchingRequirementsPtr matchingRequirements() override {
    auto requirements = std::make_unique<
        envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();

    auto* data_input_allow_list = requirements->mutable_data_input_allow_list();
    data_input_allow_list->add_type_url(
        "type.googleapis.com/"
        "envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput");
    data_input_allow_list->add_type_url(
        "type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourceIPInput");
    data_input_allow_list->add_type_url(
        "type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.SourcePortInput");
    data_input_allow_list->add_type_url(
        "type.googleapis.com/"
        "envoy.extensions.matching.common_inputs.network.v3.DirectSourceIPInput");
    data_input_allow_list->add_type_url(
        "type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput");

    return requirements;
  }
};

TEST(MatchWrapper, WithMatcher) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedNetworkFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
xds_matcher:
  matcher_tree:
    input:
      name: destination-ip
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  auto status_or_cb = match_delegate_config.createFilterFactoryFromProto(config, factory_context);
  ASSERT_TRUE(status_or_cb.ok());
  auto cb = status_or_cb.value();

  // Set up filter manager and test that filters are created and added properly
  NiceMock<Envoy::Network::MockFilterManager> filter_manager;

  // The delegate filter should be created
  EXPECT_CALL(filter_manager, addReadFilter(_))
      .WillOnce(Invoke([](Envoy::Network::ReadFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingNetworkFilter*>(filter.get()));
      }));
  EXPECT_CALL(filter_manager, addWriteFilter(_))
      .WillOnce(Invoke([](Envoy::Network::WriteFilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingNetworkFilter*>(filter.get()));
      }));
  EXPECT_CALL(filter_manager, addFilter(_))
      .WillOnce(Invoke([](Envoy::Network::FilterSharedPtr filter) {
        EXPECT_NE(nullptr, dynamic_cast<DelegatingNetworkFilter*>(filter.get()));
      }));

  cb(filter_manager);
}

TEST(MatchWrapper, WithMatcherInvalidDataInput) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedNetworkFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
xds_matcher:
  matcher_tree:
    input:
      name: invalid-input
      typed_config:
        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
        header_name: default-matcher-header
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  EXPECT_THROW_WITH_REGEX(
      match_delegate_config.createFilterFactoryFromProto(config, factory_context).value(),
      EnvoyException, ".*Didn't find a registered implementation for.*");
}

TEST(MatchWrapper, WithMatcherDataInputNotInRequirement) {
  TestFactory test_factory;
  Envoy::Registry::InjectFactory<Envoy::Server::Configuration::NamedNetworkFilterConfigFactory>
      inject_factory(test_factory);

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  const auto config =
      TestUtility::parseYaml<envoy::extensions::common::matching::v3::ExtensionWithMatcher>(R"EOF(
extension_config:
  name: test
  typed_config:
    "@type": type.googleapis.com/google.protobuf.StringValue
xds_matcher:
  matcher_tree:
    input:
      name: not-in-requirement-input
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput
    exact_match_map:
        map:
            match:
                action:
                    name: skip
                    typed_config:
                        "@type": type.googleapis.com/envoy.extensions.filters.common.matcher.action.v3.SkipFilter
)EOF");

  MatchDelegateConfig match_delegate_config;
  EXPECT_THROW_WITH_REGEX(
      match_delegate_config.createFilterFactoryFromProto(config, factory_context).value(),
      EnvoyException, ".*requirement violation while creating match tree.*");
}

TEST(DelegatingNetworkFilter, NoMatchTreeSkipFilter) {
  // Test that when no match tree is provided, the filter is skipped
  std::shared_ptr<Envoy::Network::MockReadFilter> read_filter(new Envoy::Network::MockReadFilter());
  std::shared_ptr<Envoy::Network::MockWriteFilter> write_filter(
      new Envoy::Network::MockWriteFilter());
  NiceMock<Envoy::Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Envoy::Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Envoy::Network::MockConnectionSocket> socket;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // The underlying filter's onNewConnection, onData, and onWrite should not be called
  EXPECT_CALL(*read_filter, onNewConnection()).Times(0);
  EXPECT_CALL(*read_filter, onData(_, _)).Times(0);
  EXPECT_CALL(*write_filter, onWrite(_, _)).Times(0);

  // Create filter with null match tree
  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(nullptr, read_filter, write_filter);

  delegating_filter->initializeReadFilterCallbacks(read_callbacks);
  delegating_filter->initializeWriteFilterCallbacks(write_callbacks);

  // When a callback is made with a null match tree, it should return Continue
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onNewConnection());

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onData(buffer, false));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onWrite(buffer, false));
}

template <class InputType, class ActionType>
Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData>
createMatchingTree(const std::string& name, const std::string& value) {
  auto tree = *Matcher::ExactMapMatcher<Envoy::Network::MatchingData>::create(
      std::make_unique<InputType>(name), absl::nullopt);

  tree->addChild(value, Matcher::OnMatch<Envoy::Network::MatchingData>{
                            std::make_shared<ActionType>(), nullptr, false});

  return tree;
}

class DestinationIPInput : public Matcher::DataInput<Envoy::Network::MatchingData> {
public:
  DestinationIPInput() = default;
  DestinationIPInput(const std::string&) {} // Constructor that takes a string for the tests

  Matcher::DataInputGetResult get(const Envoy::Network::MatchingData&) const override {
    return Matcher::DataInputGetResult{
        Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, "127.0.0.1"};
  }
};

TEST(DelegatingNetworkFilter, MatchTreeSkipAction) {
  // Test case for a filter with a match tree that evaluates to skip
  std::shared_ptr<Envoy::Network::MockReadFilter> read_filter(new Envoy::Network::MockReadFilter());
  std::shared_ptr<Envoy::Network::MockWriteFilter> write_filter(
      new Envoy::Network::MockWriteFilter());
  NiceMock<Envoy::Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Envoy::Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Envoy::Network::MockConnectionSocket> socket;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // The underlying filter's onNewConnection, onData, and onWrite should not be called
  EXPECT_CALL(*read_filter, onNewConnection()).Times(0);
  EXPECT_CALL(*read_filter, onData(_, _)).Times(0);
  EXPECT_CALL(*write_filter, onWrite(_, _)).Times(0);

  // Create a match tree that will match and skip
  auto match_tree =
      createMatchingTree<DestinationIPInput, SkipAction>("destination_ip", "127.0.0.1");

  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(match_tree, read_filter, write_filter);

  // Setup callbacks - this should set up the match state
  EXPECT_CALL(read_callbacks, socket()).WillRepeatedly(ReturnRef(socket));
  EXPECT_CALL(read_callbacks, connection())
      .WillRepeatedly(testing::Invoke([&]() -> Envoy::Network::Connection& {
        static NiceMock<Envoy::Network::MockConnection> connection;
        ON_CALL(connection, streamInfo()).WillByDefault(testing::ReturnRef(stream_info));
        return connection;
      }));

  delegating_filter->initializeReadFilterCallbacks(read_callbacks);
  delegating_filter->initializeWriteFilterCallbacks(write_callbacks);

  // When a callback is made with a match tree that says "skip", it should return Continue
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onNewConnection());

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onData(buffer, false));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onWrite(buffer, false));
}

TEST(DelegatingNetworkFilter, DelegateToFilters) {
  // Test that calls are properly delegated to the underlying filters
  std::shared_ptr<Envoy::Network::MockReadFilter> read_filter(new Envoy::Network::MockReadFilter());
  std::shared_ptr<Envoy::Network::MockWriteFilter> write_filter(
      new Envoy::Network::MockWriteFilter());
  NiceMock<Envoy::Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Envoy::Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Envoy::Network::MockConnectionSocket> socket;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Setup matchers to pass through (no skip)
  class NoMatchInput : public Matcher::DataInput<Envoy::Network::MatchingData> {
  public:
    NoMatchInput() = default;
    NoMatchInput(const std::string&) {} // Constructor that takes a string for the tests

    Matcher::DataInputGetResult get(const Envoy::Network::MatchingData&) const override {
      return Matcher::DataInputGetResult{
          Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, "no_match"};
    }
  };

  // Create a match tree that will not match and thus not skip
  auto match_tree =
      createMatchingTree<NoMatchInput, SkipAction>("no_match_input", "will_not_match");

  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(match_tree, read_filter, write_filter);

  EXPECT_CALL(read_callbacks, socket()).WillRepeatedly(ReturnRef(socket));
  EXPECT_CALL(read_callbacks, connection())
      .WillRepeatedly(testing::Invoke([&]() -> Envoy::Network::Connection& {
        static NiceMock<Envoy::Network::MockConnection> connection;
        ON_CALL(connection, streamInfo()).WillByDefault(testing::ReturnRef(stream_info));
        return connection;
      }));

  delegating_filter->initializeReadFilterCallbacks(read_callbacks);
  delegating_filter->initializeWriteFilterCallbacks(write_callbacks);

  // The underlying filter methods should be called
  EXPECT_CALL(*read_filter, onNewConnection())
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::Continue));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onNewConnection());

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*read_filter, onData(_, false))
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::StopIteration));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, delegating_filter->onData(buffer, false));

  EXPECT_CALL(*write_filter, onWrite(_, false))
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::StopIteration));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, delegating_filter->onWrite(buffer, false));
}

TEST(DelegatingNetworkFilterManager, RemoveReadFilterAndInitializeReadFilters) {
  NiceMock<Envoy::Network::MockFilterManager> mock_filter_manager;
  Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData> match_tree = nullptr;

  // Create the DelegatingNetworkFilterManager to test
  Factory::DelegatingNetworkFilterManager delegating_manager(mock_filter_manager, match_tree);

  // Create a filter to be removed
  std::shared_ptr<CustomMockReadFilter> read_filter(new CustomMockReadFilter());

  // Test removeReadFilter - this is a no-op in the implementation,
  // but we need to call it for coverage
  delegating_manager.removeReadFilter(read_filter);

  // Test initializeReadFilters - this always returns false in the implementation,
  // but we need to call it for coverage
  EXPECT_FALSE(delegating_manager.initializeReadFilters());
}

// Custom action type for testing non-skip action
class TestAction : public Matcher::ActionBase<ProtobufWkt::StringValue> {
public:
  explicit TestAction(const std::string& value = "test_value") : value_(value) {}

  const std::string& value() const { return value_; }

private:
  std::string value_;
};

// Test creating a match tree with TestAction
template <class InputType>
Matcher::MatchTreeSharedPtr<Envoy::Network::MatchingData>
createMatchingTreeWithTestAction(const std::string& name, const std::string& value) {
  auto tree = *Matcher::ExactMapMatcher<Envoy::Network::MatchingData>::create(
      std::make_unique<InputType>(name), absl::nullopt);

  tree->addChild(value, Matcher::OnMatch<Envoy::Network::MatchingData>{
                            std::make_shared<TestAction>(), nullptr, false});
  return tree;
}

TEST(DelegatingNetworkFilter, NonSkipActionResult) {
  // Test case for a filter with a match tree that evaluates to a non-skip action
  std::shared_ptr<Envoy::Network::MockReadFilter> read_filter(new Envoy::Network::MockReadFilter());
  std::shared_ptr<Envoy::Network::MockWriteFilter> write_filter(
      new Envoy::Network::MockWriteFilter());
  NiceMock<Envoy::Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Envoy::Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Envoy::Network::MockConnectionSocket> socket;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // With a TestAction result (not a SkipAction), the underlying filter methods should be called
  EXPECT_CALL(*read_filter, onNewConnection())
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::Continue));
  EXPECT_CALL(*read_filter, onData(_, _))
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::Continue));
  EXPECT_CALL(*write_filter, onWrite(_, _))
      .WillOnce(testing::Return(Envoy::Network::FilterStatus::Continue));

  // Create a match tree that will match but return a TestAction (not a SkipAction)
  auto match_tree =
      createMatchingTreeWithTestAction<DestinationIPInput>("destination_ip", "127.0.0.1");

  auto delegating_filter =
      std::make_shared<DelegatingNetworkFilter>(match_tree, read_filter, write_filter);

  // Setup callbacks - this should set up the match state
  EXPECT_CALL(read_callbacks, socket()).WillRepeatedly(ReturnRef(socket));
  EXPECT_CALL(read_callbacks, connection())
      .WillRepeatedly(testing::Invoke([&]() -> Envoy::Network::Connection& {
        static NiceMock<Envoy::Network::MockConnection> connection;
        ON_CALL(connection, streamInfo()).WillByDefault(testing::ReturnRef(stream_info));
        return connection;
      }));

  delegating_filter->initializeReadFilterCallbacks(read_callbacks);
  delegating_filter->initializeWriteFilterCallbacks(write_callbacks);

  // When a callback is made with a match tree that returns a non-skip action,
  // it should delegate to the underlying filter
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onNewConnection());

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onData(buffer, false));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, delegating_filter->onWrite(buffer, false));
}

} // namespace
} // namespace MatchDelegate
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
