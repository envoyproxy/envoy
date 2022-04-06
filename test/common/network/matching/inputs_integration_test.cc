#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

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

class InputsIntegrationTest : public ::testing::Test {
public:
  InputsIntegrationTest()
      : inject_action_(action_factory_), context_(""),
        matcher_factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void initialize(const std::string& input, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(std::string(yaml), input, value), matcher,
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
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DestinationPortInput) {
  initialize("DestinationPortInput", "8080");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourceIPInput) {
  initialize("SourceIPInput", "127.0.0.1");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourcePortInput) {
  initialize("SourcePortInput", "8080");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DirectSourceIPInput) {
  initialize("DirectSourceIPInput", "127.0.0.1");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setDirectRemoteAddressForTest(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourceTypeInput) {
  initialize("SourceTypeInput", "local");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, ServerNameInput) {
  initialize("ServerNameInput", "example.com");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  EXPECT_CALL(socket, requestedServerName).WillOnce(testing::Return("example.com"));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, TransportProtocolInput) {
  initialize("TransportProtocolInput", "tls");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  EXPECT_CALL(socket, detectedTransportProtocol).WillOnce(testing::Return("tls"));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, ApplicationProtocolInput) {
  initialize("ApplicationProtocolInput", "'http/1.1'");

  Network::MockConnectionSocket socket;
  MatchingDataImpl data(socket);
  std::vector<std::string> protocols = {"http/1.1"};
  EXPECT_CALL(socket, requestedApplicationProtocols).WillOnce(testing::ReturnRef(protocols));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
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
    MessageUtil::loadFromYaml(fmt::format(std::string(yaml), input, value), matcher,
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
