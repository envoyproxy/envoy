#include "extensions/filters/http/jwt_authn/filter_config.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class AllVerifierTest : public ::testing::Test {
public:
  void SetUp() { MessageUtil::loadFromYaml(ExampleConfig, proto_config_); }

  void createVerifier() {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules()[0].requires(), proto_config_.providers(),
                                 *filter_config_, filter_config_->getExtractor());
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  VerifierPtr verifier_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ContextSharedPtr context_;
  MockVerifierCallbacks mock_cb_;
};

// tests rule that is just match no requries.
TEST_F(AllVerifierTest, TestAllAllow) {
  proto_config_.mutable_rules(0)->clear_requires();
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).Times(2).WillRepeatedly(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer a"}};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// tests requires allow missing or failed is true
TEST_F(AllVerifierTest, TestAllowFailedTrue) {
  std::vector<std::string> names{"a", "b", "c"};
  for (const auto& it : names) {
    auto header =
        (*proto_config_.mutable_providers())[std::string(ProviderName)].add_from_headers();
    header->set_name(it);
    header->set_value_prefix("Prefix ");
  }
  proto_config_.mutable_rules(0)->mutable_requires()->mutable_allow_missing_or_failed()->set_value(
      true);
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"a", "Prefix " + std::string(GoodToken)},
      {"b", "Prefix " + std::string(InvalidAudToken)},
      {"c", "Prefix "},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("a"));
  EXPECT_TRUE(headers.has("b"));
  EXPECT_TRUE(headers.has("c"));
}

// test requires allow missing or failed is false
TEST_F(AllVerifierTest, TestAllowFailedFalse) {
  proto_config_.mutable_rules(0)->mutable_requires()->mutable_allow_missing_or_failed()->set_value(
      false);
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_))
      .Times(2)
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::JwtExpired); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::JwtMissed); }));
  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(ExpiredToken)},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
