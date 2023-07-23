#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/matching/network/application_protocol/config.h"
#include "source/extensions/matching/network/common/inputs.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Matching {

constexpr absl::string_view yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.{}
  exact_match_map:
    map:
      "{}":
        action:
          name: test_action
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: foo
)EOF";

constexpr absl::string_view yaml_filter_state = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.network.v3.FilterStateInput
      key: {}
  exact_match_map:
    map:
      "{}":
        action:
          name: test_action
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: foo
)EOF";

class InputsIntegrationTest : public ::testing::Test {
public:
  InputsIntegrationTest()
      : inject_action_(action_factory_), context_(""),
        matcher_factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void initialize(const std::string& input, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(yaml, input, value), matcher,
                              ProtobufMessage::getStrictValidationVisitor());

    match_tree_ = matcher_factory_.create(matcher);
  }

  void initializeFilterStateCase(const std::string& key, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(yaml_filter_state, key, value), matcher,
                              ProtobufMessage::getStrictValidationVisitor());

    match_tree_ = matcher_factory_.create(matcher);
  }

protected:
  Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Matcher::ActionFactory<absl::string_view>> inject_action_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Matcher::MockMatchTreeValidationVisitor<Network::MatchingData> validation_visitor_;
  absl::string_view context_;
  Matcher::MatchTreeFactory<Network::MatchingData, absl::string_view> matcher_factory_;
  Matcher::MatchTreeFactoryCb<Network::MatchingData> match_tree_;
};

TEST_F(InputsIntegrationTest, DestinationIPInput) {
  initialize("DestinationIPInput", "127.0.0.1");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DestinationPortInput) {
  initialize("DestinationPortInput", "8080");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourceIPInput) {
  initialize("SourceIPInput", "127.0.0.1");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourcePortInput) {
  initialize("SourcePortInput", "8080");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DirectSourceIPInput) {
  initialize("DirectSourceIPInput", "127.0.0.1");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setDirectRemoteAddressForTest(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourceTypeInput) {
  initialize("SourceTypeInput", "local");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, ServerNameInput) {
  const auto host = "example.com";
  initialize("ServerNameInput", host);

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  socket.connection_info_provider_->setRequestedServerName(host);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, TransportProtocolInput) {
  initialize("TransportProtocolInput", "tls");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return("tls"));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, ApplicationProtocolInput) {
  initialize("ApplicationProtocolInput", "'http/1.1'");

  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);
  std::vector<std::string> protocols = {"http/1.1"};
  EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, FilterStateInput) {
  std::string key = "filter_state_key";
  std::string value = "filter_state_value";
  initializeFilterStateCase(key, value);

  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>(value),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  Network::MockConnectionSocket socket;
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DynamicMetadataInput) {
  Network::MockConnectionSocket socket;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  std::string metadata_key("metadata_key");
  std::string label_key("label_key");
  auto label = MessageUtil::keyValueStruct(label_key, "bar");
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>(metadata_key, label));
  auto stored_metadata = data.dynamicMetadata().filter_metadata();
  EXPECT_EQ(label.fields_size(), 1);
  EXPECT_EQ(stored_metadata[metadata_key].fields_size(), 1);
  EXPECT_EQ((*label.mutable_fields())[label_key].string_value(),
            (*stored_metadata[metadata_key].mutable_fields())[label_key].string_value());
}

TEST_F(InputsIntegrationTest, FilterStateInputFailure) {
  std::string key = "filter_state_key";
  std::string value = "filter_state_value";
  initializeFilterStateCase(key, value);

  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  Network::MockConnectionSocket socket;
  envoy::config::core::v3::Metadata metadata;
  MatchingDataImpl data(socket, filter_state, metadata);

  // No filter state object - no match
  const auto result_no_fs = match_tree_()->match(data);
  EXPECT_EQ(result_no_fs.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_FALSE(result_no_fs.on_match_.has_value());

  filter_state.setData("unknown_key", std::make_shared<Router::StringAccessorImpl>(value),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  // Unknown key in filter state - no match
  const auto result_no_key = match_tree_()->match(data);
  EXPECT_EQ(result_no_key.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_FALSE(result_no_key.on_match_.has_value());

  filter_state.setData(key, std::make_shared<Router::StringAccessorImpl>("unknown_value"),
                       StreamInfo::FilterState::StateType::Mutable,
                       StreamInfo::FilterState::LifeSpan::Connection);

  // Known key in filter state but unknown value - no match
  const auto result_no_value = match_tree_()->match(data);
  EXPECT_EQ(result_no_value.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_FALSE(result_no_value.on_match_.has_value());
}

class UdpInputsIntegrationTest : public ::testing::Test {
public:
  UdpInputsIntegrationTest()
      : inject_action_(action_factory_), context_(""),
        matcher_factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void initialize(const std::string& input, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(yaml, input, value), matcher,
                              ProtobufMessage::getStrictValidationVisitor());

    match_tree_ = matcher_factory_.create(matcher);
  }

protected:
  Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Matcher::ActionFactory<absl::string_view>> inject_action_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Matcher::MockMatchTreeValidationVisitor<Network::UdpMatchingData> validation_visitor_;
  absl::string_view context_;
  Matcher::MatchTreeFactory<Network::UdpMatchingData, absl::string_view> matcher_factory_;
  Matcher::MatchTreeFactoryCb<Network::UdpMatchingData> match_tree_;
};

TEST_F(UdpInputsIntegrationTest, DestinationIPInput) {
  initialize("DestinationIPInput", "127.0.0.1");

  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  UdpMatchingDataImpl data(ip, ip);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(UdpInputsIntegrationTest, DestinationPortInput) {
  initialize("DestinationPortInput", "8080");

  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  UdpMatchingDataImpl data(ip, ip);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(UdpInputsIntegrationTest, SourceIPInput) {
  initialize("SourceIPInput", "127.0.0.1");

  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  UdpMatchingDataImpl data(ip, ip);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(UdpInputsIntegrationTest, SourcePortInput) {
  initialize("SourcePortInput", "8080");

  const Address::Ipv4Instance ip("127.0.0.1", 8080);
  UdpMatchingDataImpl data(ip, ip);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

} // namespace Matching
} // namespace Network
} // namespace Envoy
