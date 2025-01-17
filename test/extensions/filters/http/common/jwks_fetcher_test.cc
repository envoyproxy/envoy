#include <chrono>
#include <thread>

#include "envoy/config/core/v3/http_uri.pb.h"

#include "source/common/http/message_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"

#include "test/extensions/filters/http/common/mock.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
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

const std::string config = R"(
http_uri:
  uri: https://pubkey_server/pubkey_path
  cluster: pubkey_cluster
  timeout:
    seconds: 5
)";

class JwksFetcherTest : public testing::Test {
public:
  void setupFetcher(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, remote_jwks_);
    mock_factory_ctx_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"pubkey_cluster"});
    fetcher_ = JwksFetcher::create(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                                   remote_jwks_);
    EXPECT_TRUE(fetcher_ != nullptr);
  }

  RemoteJwks remote_jwks_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::unique_ptr<JwksFetcher> fetcher_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

// Test findByIssuer
TEST_F(JwksFetcherTest, TestGetSuccess) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           publicKey);
  MockJwksReceiver receiver;
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_));
  EXPECT_CALL(receiver, onJwksError(testing::_)).Times(0);

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestMessageHeader) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           publicKey);
  MockJwksReceiver receiver;

  // Expectations for message
  EXPECT_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
                  .async_client_,
              send_(_, _, _))
      .WillOnce(Invoke([](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks&,
                          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
        EXPECT_EQ(message->headers().getUserAgentValue(),
                  Http::Headers::get().UserAgentValues.GoBrowser);
        return nullptr;
      }));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGet400) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "400",
                           "invalid");
  MockJwksReceiver receiver;
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGetNoBody) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200", "");
  MockJwksReceiver receiver;
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestGetInvalidJwks) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           "invalid");
  MockJwksReceiver receiver;
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestHttpFailure) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                           Http::AsyncClient::FailureReason::Reset);
  MockJwksReceiver receiver;
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(JwksFetcher::JwksReceiver::Failure::Network));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

TEST_F(JwksFetcherTest, TestCancel) {
  // Setup
  setupFetcher(config);
  Http::MockAsyncClientRequest request(&(mock_factory_ctx_.server_factory_context_.cluster_manager_
                                             .thread_local_cluster_.async_client_));
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, &request);
  MockJwksReceiver receiver;
  EXPECT_CALL(request, cancel());
  EXPECT_CALL(receiver, onJwksSuccessImpl(testing::_)).Times(0);
  EXPECT_CALL(receiver, onJwksError(testing::_)).Times(0);

  // Act
  fetcher_->fetch(parent_span_, receiver);
  // Proper cancel
  fetcher_->cancel();
  // Re-entrant cancel
  fetcher_->cancel();
}

TEST_F(JwksFetcherTest, TestSpanPassedDown) {
  // Setup
  setupFetcher(config);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           publicKey);
  NiceMock<MockJwksReceiver> receiver;

  // Expectations for span
  EXPECT_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
                  .async_client_,
              send_(_, _, _))
      .WillOnce(Invoke(
          [this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
                 const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            EXPECT_TRUE(options.parent_span_ == &this->parent_span_);
            EXPECT_TRUE(options.child_span_name_ == "JWT Remote PubKey Fetch");
            return nullptr;
          }));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}
struct RetryingParameters {
  RetryingParameters(const std::string& config, uint32_t n, int64_t base_ms, int64_t max_ms)
      : config_(config), expected_num_retries_(n), expected_backoff_base_interval_ms_(base_ms),
        expected_backoff_max_interval_ms_(max_ms) {}

  std::string config_;

  uint32_t expected_num_retries_;
  int64_t expected_backoff_base_interval_ms_;
  int64_t expected_backoff_max_interval_ms_;
};

class JwksFetcherRetryingTest : public testing::TestWithParam<RetryingParameters> {
public:
  void setupFetcher(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, remote_jwks_);
    mock_factory_ctx_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"pubkey_cluster"});
    fetcher_ = JwksFetcher::create(mock_factory_ctx_.server_factory_context_.cluster_manager_,
                                   remote_jwks_);
    EXPECT_TRUE(fetcher_ != nullptr);
  }

  RemoteJwks remote_jwks_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::unique_ptr<JwksFetcher> fetcher_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

INSTANTIATE_TEST_SUITE_P(Retrying, JwksFetcherRetryingTest,
                         testing::Values(RetryingParameters{R"(
http_uri:
  uri: https://pubkey_server/pubkey_path
  cluster: pubkey_cluster
  timeout:
    seconds: 5
retry_policy:
  retry_back_off:
    base_interval: 0.1s
    max_interval: 32s
  num_retries: 10
)",
                                                            10, 100, 32000},
                                         RetryingParameters{R"(
http_uri:
  uri: https://pubkey_server/pubkey_path
  cluster: pubkey_cluster
  timeout:
    seconds: 5
retry_policy: {}
)",
                                                            1, 1000, 10000},
                                         RetryingParameters{R"(
http_uri:
  uri: https://pubkey_server/pubkey_path
  cluster: pubkey_cluster
  timeout:
    seconds: 5
retry_policy:
  num_retries: 2
)",
                                                            2, 1000, 10000}));

TEST_P(JwksFetcherRetryingTest, TestCompleteRetryPolicy) {

  // Setup
  setupFetcher(GetParam().config_);
  MockUpstream mock_pubkey(mock_factory_ctx_.server_factory_context_.cluster_manager_, "200",
                           publicKey);
  NiceMock<MockJwksReceiver> receiver;

  // Expectations for envoy.config.core.v3.RetryPolicy to envoy.config.route.v3.RetryPolicy
  // used by async client.
  // execution deep down in async_client_'s route entry implementation
  // is not exercised here, just the configuration adaptation.
  EXPECT_CALL(mock_factory_ctx_.server_factory_context_.cluster_manager_.thread_local_cluster_
                  .async_client_,
              send_(_, _, _))
      .WillOnce(Invoke(
          [](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
             const Http::AsyncClient::RequestOptions& options) -> Http::AsyncClient::Request* {
            RetryingParameters const& rp = GetParam();

            EXPECT_TRUE(options.retry_policy.has_value());
            EXPECT_TRUE(options.buffer_body_for_retry);
            EXPECT_TRUE(options.retry_policy.value().has_num_retries());
            EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(options.retry_policy.value(), num_retries),
                      rp.expected_num_retries_);

            EXPECT_TRUE(options.retry_policy.value().has_retry_back_off());
            EXPECT_TRUE(options.retry_policy.value().retry_back_off().has_base_interval());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value().retry_back_off(),
                                               base_interval),
                      rp.expected_backoff_base_interval_ms_);
            EXPECT_TRUE(options.retry_policy.value().retry_back_off().has_max_interval());
            EXPECT_EQ(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value().retry_back_off(),
                                               max_interval),
                      rp.expected_backoff_max_interval_ms_);

            EXPECT_TRUE(options.retry_policy.value().has_per_try_timeout());
            EXPECT_LE(PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value().retry_back_off(),
                                               max_interval),
                      PROTOBUF_GET_MS_REQUIRED(options.retry_policy.value(), per_try_timeout));

            const std::string& retry_on = options.retry_policy.value().retry_on();
            std::set<std::string> retry_on_modes = absl::StrSplit(retry_on, ',');

            EXPECT_EQ(retry_on_modes.count("5xx"), 1);
            EXPECT_EQ(retry_on_modes.count("gateway-error"), 1);
            EXPECT_EQ(retry_on_modes.count("connect-failure"), 1);
            EXPECT_EQ(retry_on_modes.count("reset"), 1);

            return nullptr;
          }));

  // Act
  fetcher_->fetch(parent_span_, receiver);
}

} // namespace
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
