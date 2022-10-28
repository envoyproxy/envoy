#include "source/common/http/matching/data_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/ssl/matching/inputs.h"

#include "test/common/matcher/test_utility.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

constexpr absl::string_view yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.ssl.v3.{}
  exact_match_map:
    map:
      "{}":
        action:
          name: test_action
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: foo
)EOF";

template <class MatchingDataType> class InputsIntegrationTest : public ::testing::Test {
public:
  explicit InputsIntegrationTest()
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
  Matcher::MockMatchTreeValidationVisitor<MatchingDataType> validation_visitor_;
  absl::string_view context_;
  Matcher::MatchTreeFactory<MatchingDataType, absl::string_view> matcher_factory_;
  Matcher::MatchTreeFactoryCb<MatchingDataType> match_tree_;
};

using NetworkInputsIntegrationTest = InputsIntegrationTest<Network::MatchingData>;

TEST_F(NetworkInputsIntegrationTest, UriSanInput) {
  const auto host = "example.com";

  initialize("UriSanInput", host);

  Network::MockConnectionSocket socket;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  socket.connection_info_provider_->setSslConnection(ssl);
  std::vector<std::string> uri_sans{host};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillOnce(Return(uri_sans));
  Network::Matching::MatchingDataImpl data(socket);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(NetworkInputsIntegrationTest, DnsSanInput) {
  const auto host = "example.com";

  initialize("DnsSanInput", host);

  Network::MockConnectionSocket socket;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  socket.connection_info_provider_->setSslConnection(ssl);
  std::vector<std::string> dns_sans{host};
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));
  Network::Matching::MatchingDataImpl data(socket);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(NetworkInputsIntegrationTest, SubjectInput) {
  const std::string host = "example.com";

  initialize("SubjectInput", host);

  Network::MockConnectionSocket socket;
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  socket.connection_info_provider_->setSslConnection(ssl);
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(testing::ReturnRef(host));
  Network::Matching::MatchingDataImpl data(socket);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

using HttpInputsIntegrationTest = InputsIntegrationTest<Http::HttpMatchingData>;

TEST_F(HttpInputsIntegrationTest, UriSanInput) {
  const auto host = "example.com";

  initialize("UriSanInput", host);

  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);
  std::vector<std::string> uri_sans{host};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillOnce(Return(uri_sans));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(HttpInputsIntegrationTest, DnsSanInput) {
  const auto host = "example.com";

  initialize("DnsSanInput", host);

  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);
  std::vector<std::string> dns_sans{host};
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(HttpInputsIntegrationTest, SubjectInput) {
  const std::string host = "example.com";

  initialize("SubjectInput", host);

  Network::ConnectionInfoSetterImpl connection_info_provider(
      std::make_shared<Network::Address::Ipv4Instance>(80),
      std::make_shared<Network::Address::Ipv4Instance>(80));
  std::shared_ptr<Ssl::MockConnectionInfo> ssl = std::make_shared<Ssl::MockConnectionInfo>();
  connection_info_provider.setSslConnection(ssl);
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(testing::ReturnRef(host));
  Http::Matching::HttpMatchingDataImpl data(connection_info_provider);

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
