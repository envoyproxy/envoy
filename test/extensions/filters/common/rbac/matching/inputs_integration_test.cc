#include "source/common/network/address_impl.h"
#include "source/extensions/filters/common/rbac/matching/data_impl.h"
#include "source/extensions/filters/common/rbac/matching/inputs.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matching {

constexpr absl::string_view network_yaml = R"EOF(
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

  void initializeNetwork(const std::string& input, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(std::string(network_yaml), input, value), matcher,
                              ProtobufMessage::getStrictValidationVisitor());

    match_tree_ = matcher_factory_.create(matcher);
  }

protected:
  Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Matcher::ActionFactory<absl::string_view>> inject_action_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Matcher::MockMatchTreeValidationVisitor<MatchingData> validation_visitor_;
  absl::string_view context_;
  Matcher::MatchTreeFactory<MatchingData, absl::string_view> matcher_factory_;
  Matcher::MatchTreeFactoryCb<MatchingData> match_tree_;
};

TEST_F(InputsIntegrationTest, DestinationIPInput) {
  initializeNetwork("DestinationIPInput", "127.0.0.1");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  info.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DestinationPortInput) {
  initializeNetwork("DestinationPortInput", "8080");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  info.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourceIPInput) {
  initializeNetwork("SourceIPInput", "127.0.0.1");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  info.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SourcePortInput) {
  initializeNetwork("SourcePortInput", "8080");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  info.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DirectSourceIPInput) {
  initializeNetwork("DirectSourceIPInput", "127.0.0.1");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, ServerNameInput) {
  initializeNetwork("ServerNameInput", "example.com");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  MatchingDataImpl data(conn, headers, info);
  EXPECT_CALL(conn, requestedServerName).WillOnce(testing::Return("example.com"));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

} // namespace Matching
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
