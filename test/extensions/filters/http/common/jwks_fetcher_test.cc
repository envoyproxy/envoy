#include <chrono>
#include <thread>

#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/jwks_fetcher.h"

#include "test/extensions/filters/http/common/mock.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

using ::envoy::api::v2::core::HttpUri;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

const char publicKey[] = R"(
{
  "keys": [
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
      "n": "up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1qmUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrkU7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaEWopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_ZdboY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw",
      "e": "AQAB"
    },
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",
      "n": "up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1qmUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrkU7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaEWopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_ZdboY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw",
      "e": "AQAB"
    }
  ]
}
)";

const std::string JwksUri = R"(
uri: https://pubkey_server/pubkey_path
cluster: pubkey_cluster
timeout:
  seconds: 5
)";

class JwksFetcherTest : public testing::Test {
public:
  void SetUp() override { TestUtility::loadFromYaml(JwksUri, uri_); }
  HttpUri uri_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

// Test findByIssuer
TEST_F(JwksFetcherTest, TestGetSuccess) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "200", publicKey);
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(1);
  EXPECT_CALL(receiver, onJwksError(testing::_)).Times(0);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGet400) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "400", "invalid");
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network)).Times(1);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGetNoBody) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "200", "");
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network)).Times(1);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGetInvalidJwks) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "200", "invalid");
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks)).Times(1);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestHttpFailure) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_,
                           Http::AsyncClient::FailureReason::Reset);
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network)).Times(1);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestCancel) {
  // Setup
  Http::MockAsyncClientRequest request(&(mock_factory_ctx_.cluster_manager_.async_client_));
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, &request);
  MockJwksReceiver receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));
  EXPECT_TRUE(fetcher != nullptr);
  EXPECT_CALL(request, cancel()).Times(1);
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(testing::_)).Times(0);

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
  // Proper cancel
  fetcher->cancel();
  // Re-entrant cancel
  fetcher->cancel();
}

TEST_F(JwksFetcherTest, TestSpanPassedDown) {
  // Setup
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "200", publicKey);
  NiceMock<MockJwksReceiver> receiver;
  std::unique_ptr<JwksFetcher> fetcher(JwksFetcher::create(mock_factory_ctx_.cluster_manager_));

  // Expectations for span
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [this](Http::MessagePtr&, Http::AsyncClient::Callbacks&,
                 const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.parent_span_ == &this->parent_span_);
            EXPECT_TRUE(options.child_span_name_ == "JWT Remote PubKey Fetch");
            return nullptr;
          }));

  // Act
  fetcher->fetch(uri_, parent_span_, receiver);
}

} // namespace
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
