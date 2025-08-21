#include "source/extensions/filters/http/jwt_authn/jwks_async_fetcher.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"

using envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks;
using Envoy::Extensions::HttpFilters::Common::JwksFetcherPtr;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

JwtAuthnFilterStats generateMockStats(Stats::Scope& scope) {
  return {ALL_JWT_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, ""))};
}

class MockJwksFetcher : public Common::JwksFetcher {
public:
  using SaveJwksReceiverFn = std::function<void(JwksReceiver& receiver)>;
  MockJwksFetcher(SaveJwksReceiverFn receiver_fn) : receiver_fn_(receiver_fn) {}

  void cancel() override {}
  void fetch(Tracing::Span&, JwksReceiver& receiver) override { receiver_fn_(receiver); }

private:
  SaveJwksReceiverFn receiver_fn_;
};

// TestParam is for fast_listener,
class JwksAsyncFetcherTest : public testing::TestWithParam<bool> {
public:
  JwksAsyncFetcherTest() : stats_(generateMockStats(context_.scope())) {}

  // init manager is used in is_slow_listener mode
  bool initManagerUsed() const {
    return config_.has_async_fetch() && !config_.async_fetch().fast_listener();
  }

  void setupAsyncFetcher(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, config_);
    if (config_.has_async_fetch()) {
      // Param is for fast_listener,
      if (GetParam()) {
        config_.mutable_async_fetch()->set_fast_listener(true);
      }
    }

    if (initManagerUsed()) {
      EXPECT_CALL(context_.init_manager_, add(_))
          .WillOnce(Invoke([this](const Init::Target& target) {
            init_target_handle_ = target.createHandle("test");
          }));
    }

    // if async_fetch is enabled, timer is created
    if (config_.has_async_fetch()) {
      timer_ = new NiceMock<Event::MockTimer>(&context_.server_factory_context_.dispatcher_);
    }

    async_fetcher_ = std::make_unique<JwksAsyncFetcher>(
        config_, context_,
        [this](Upstream::ClusterManager&, const RemoteJwks&) {
          return std::make_unique<MockJwksFetcher>(
              [this](Common::JwksFetcher::JwksReceiver& receiver) {
                fetch_receiver_array_.push_back(&receiver);
              });
        },
        stats_,
        [this](google::jwt_verify::JwksPtr&& jwks) { out_jwks_array_.push_back(std::move(jwks)); });

    if (initManagerUsed()) {
      init_target_handle_->initialize(init_watcher_);
    }
  }

  RemoteJwks config_;
  JwksAsyncFetcherPtr async_fetcher_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  JwtAuthnFilterStats stats_;
  std::vector<Common::JwksFetcher::JwksReceiver*> fetch_receiver_array_;
  std::vector<google::jwt_verify::JwksPtr> out_jwks_array_;

  Init::TargetHandlePtr init_target_handle_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
  Event::MockTimer* timer_{};
};

INSTANTIATE_TEST_SUITE_P(JwksAsyncFetcherTest, JwksAsyncFetcherTest,
                         testing::ValuesIn({false, true}));

TEST_P(JwksAsyncFetcherTest, TestNotAsyncFetch) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
)";

  setupAsyncFetcher(config);
  // fetch is not called
  EXPECT_EQ(fetch_receiver_array_.size(), 0);
  // Not Jwks output
  EXPECT_EQ(out_jwks_array_.size(), 0);
  // init_watcher ready is not called.
  init_watcher_.expectReady().Times(0);

  EXPECT_EQ(0U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(0U, stats_.jwks_fetch_failed_.value());
}

TEST_P(JwksAsyncFetcherTest, TestGoodFetch) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  setupAsyncFetcher(config);
  // Jwks response is not received yet
  EXPECT_EQ(out_jwks_array_.size(), 0);

  if (initManagerUsed()) {
    // Verify ready is not called.
    init_watcher_.expectReady().Times(0);
    EXPECT_TRUE(::testing::Mock::VerifyAndClearExpectations(&init_watcher_));
    init_watcher_.expectReady();
  }

  // Trigger the Jwks response
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
  auto jwks = google::jwt_verify::Jwks::createFrom(PublicKey, google::jwt_verify::Jwks::JWKS);
  fetch_receiver_array_[0]->onJwksSuccess(std::move(jwks));

  // Output 1 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 1);

  EXPECT_EQ(1U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(0U, stats_.jwks_fetch_failed_.value());
}

TEST_P(JwksAsyncFetcherTest, TestNetworkFailureFetch) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  // Just start the Jwks fetch call
  setupAsyncFetcher(config);
  // Jwks response is not received yet
  EXPECT_EQ(out_jwks_array_.size(), 0);

  if (initManagerUsed()) {
    // Verify ready is not called.
    init_watcher_.expectReady().Times(0);
    EXPECT_TRUE(::testing::Mock::VerifyAndClearExpectations(&init_watcher_));
    // Verify ready is called.
    init_watcher_.expectReady();
  }

  // Trigger the Jwks response
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
  fetch_receiver_array_[0]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);

  // Output 0 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 0);

  EXPECT_EQ(0U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(1U, stats_.jwks_fetch_failed_.value());
}

TEST_P(JwksAsyncFetcherTest, TestGoodFetchAndRefresh) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  setupAsyncFetcher(config);
  // Initial fetch is successful
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
  auto jwks = google::jwt_verify::Jwks::createFrom(PublicKey, google::jwt_verify::Jwks::JWKS);
  fetch_receiver_array_[0]->onJwksSuccess(std::move(jwks));

  // Output 1 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 1);

  // Expect refresh timer is enabled.
  constexpr std::chrono::seconds refetchBeforeExpiredSec(5);
  const std::chrono::milliseconds expected_refetch_time =
      JwksAsyncFetcher::getCacheDuration(config_) - refetchBeforeExpiredSec;
  EXPECT_CALL(*timer_, enableTimer(expected_refetch_time, nullptr));
  timer_->invokeCallback();

  // refetch again after cache duration interval: successful.
  EXPECT_EQ(fetch_receiver_array_.size(), 2);
  auto jwks1 = google::jwt_verify::Jwks::createFrom(PublicKey, google::jwt_verify::Jwks::JWKS);
  fetch_receiver_array_[1]->onJwksSuccess(std::move(jwks1));

  // Output 2 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 2);
  EXPECT_EQ(2U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(0U, stats_.jwks_fetch_failed_.value());
}

TEST_P(JwksAsyncFetcherTest, TestNetworkFailureFetchWithDefaultRefetch) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  // Just start the Jwks fetch call
  setupAsyncFetcher(config);
  // first fetch: network failure.
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
  fetch_receiver_array_[0]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);

  // Output 0 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 0);

  // Expect refresh timer is enabled.
  // Default refetch time for a failed one is 1 second.
  const std::chrono::milliseconds expected_refetch_time = std::chrono::seconds(1);
  EXPECT_CALL(*timer_, enableTimer(expected_refetch_time, nullptr));
  timer_->invokeCallback();

  // refetch again after cache duration interval: network failure.
  EXPECT_EQ(fetch_receiver_array_.size(), 2);
  fetch_receiver_array_[1]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);

  // Output 0 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 0);
  EXPECT_EQ(0U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(2U, stats_.jwks_fetch_failed_.value());
}

TEST_P(JwksAsyncFetcherTest, TestNetworkFailureFetchWithCustomRefetch) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch:
        failed_refetch_duration:
          seconds: 10
)";

  // Just start the Jwks fetch call
  setupAsyncFetcher(config);
  // first fetch: network failure.
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
  fetch_receiver_array_[0]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);

  // Output 0 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 0);

  // Expect refresh timer is enabled.
  const std::chrono::milliseconds expected_refetch_time = std::chrono::seconds(10);
  EXPECT_CALL(*timer_, enableTimer(expected_refetch_time, nullptr));
  timer_->invokeCallback();

  // refetch again after cache duration interval: network failure.
  EXPECT_EQ(fetch_receiver_array_.size(), 2);
  fetch_receiver_array_[1]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);

  // Output 0 jwks.
  EXPECT_EQ(out_jwks_array_.size(), 0);
  EXPECT_EQ(0U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(2U, stats_.jwks_fetch_failed_.value());
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
