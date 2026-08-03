#include "source/common/router/retry_policy_impl.h"
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

    Router::RetryPolicyConstSharedPtr retry_policy = nullptr;
    if (config_.has_retry_policy()) {
      envoy::config::route::v3::RetryPolicy route_retry_policy =
          Http::Utility::convertCoreToRouteRetryPolicy(config_.retry_policy(),
                                                       "5xx,gateway-error,connect-failure,reset");
      // Use the null validation visitor because it was used by the async client in the previous
      // implementation.
      auto policy_or_error = Router::RetryPolicyImpl::create(
          route_retry_policy, ProtobufMessage::getNullValidationVisitor(),
          context_.serverFactoryContext());
      THROW_IF_NOT_OK_REF(policy_or_error.status());
      retry_policy = std::move(policy_or_error.value());
    }

    async_fetcher_ = std::make_unique<JwksAsyncFetcher>(
        config_, std::move(retry_policy), context_,
        [this](Upstream::ClusterManager&, Router::RetryPolicyConstSharedPtr, const RemoteJwks&) {
          return std::make_unique<MockJwksFetcher>(
              [this](Common::JwksFetcher::JwksReceiver& receiver) {
                fetch_receiver_array_.push_back(&receiver);
              });
        },
        stats_,
        [this](Envoy::JwtVerify::JwksPtr&& jwks) { out_jwks_array_.push_back(std::move(jwks)); });

    if (initManagerUsed()) {
      init_target_handle_->initialize(init_watcher_);
    }
  }

  RemoteJwks config_;
  JwksAsyncFetcherPtr async_fetcher_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  JwtAuthnFilterStats stats_;
  std::vector<Common::JwksFetcher::JwksReceiver*> fetch_receiver_array_;
  std::vector<Envoy::JwtVerify::JwksPtr> out_jwks_array_;

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
  auto jwks = Envoy::JwtVerify::Jwks::createFrom(PublicKey, Envoy::JwtVerify::Jwks::JWKS);
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
  auto jwks = Envoy::JwtVerify::Jwks::createFrom(PublicKey, Envoy::JwtVerify::Jwks::JWKS);
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
  auto jwks1 = Envoy::JwtVerify::Jwks::createFrom(PublicKey, Envoy::JwtVerify::Jwks::JWKS);
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

// When the owning filter chain starts draining while a fetch is in flight, the fetch completion
// must not re-arm the refetch timer, so the async fetch loop stops.
TEST_P(JwksAsyncFetcherTest, TestNoRefetchScheduledOnFailureWhenDraining) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  // The initial fetch happens while the filter chain is not draining.
  setupAsyncFetcher(config);
  EXPECT_EQ(fetch_receiver_array_.size(), 1);

  // The owning filter chain starts draining, e.g. it was replaced by an in-place filter chain
  // update. From now on drainClose() returns true.
  ON_CALL(context_.drain_manager_, drainClose(_)).WillByDefault(testing::Return(true));

  // When the in-flight fetch completes, the refetch timer must NOT be re-armed.
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  fetch_receiver_array_[0]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);
  EXPECT_EQ(out_jwks_array_.size(), 0);
  EXPECT_EQ(1U, stats_.jwks_fetch_failed_.value());
  // Timer left disabled: no further fetch loop.
  EXPECT_FALSE(timer_->enabled());
}

// When the owning filter chain starts draining while a fetch is in flight, a successful fetch still
// delivers the Jwks but must not re-arm the refetch timer, so the async fetch loop stops.
TEST_P(JwksAsyncFetcherTest, TestNoRefetchScheduledOnSuccessWhenDraining) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  // The initial fetch happens while the filter chain is not draining.
  setupAsyncFetcher(config);
  EXPECT_EQ(fetch_receiver_array_.size(), 1);

  // The owning filter chain starts draining.
  ON_CALL(context_.drain_manager_, drainClose(_)).WillByDefault(testing::Return(true));

  // When the in-flight fetch succeeds, the Jwks is still delivered but the refetch timer must NOT
  // be re-armed.
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  auto jwks = Envoy::JwtVerify::Jwks::createFrom(PublicKey, Envoy::JwtVerify::Jwks::JWKS);
  fetch_receiver_array_[0]->onJwksSuccess(std::move(jwks));
  EXPECT_EQ(out_jwks_array_.size(), 1);
  EXPECT_EQ(1U, stats_.jwks_fetch_success_.value());
  // Timer left disabled: no further fetch loop.
  EXPECT_FALSE(timer_->enabled());
}

// A refetch timer that was armed before the filter chain started draining must be a no-op when it
// fires: fetch() bails out without starting a new fetch or re-arming the timer.
TEST_P(JwksAsyncFetcherTest, TestArmedTimerNoOpWhenDraining) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  setupAsyncFetcher(config);
  EXPECT_EQ(fetch_receiver_array_.size(), 1);

  // Complete the initial fetch while not draining: the refetch timer is armed.
  fetch_receiver_array_[0]->onJwksError(Common::JwksFetcher::JwksReceiver::Failure::Network);
  EXPECT_TRUE(timer_->enabled());

  // Now the owning filter chain starts draining.
  ON_CALL(context_.drain_manager_, drainClose(_)).WillByDefault(testing::Return(true));

  // The armed timer fires: fetch() must bail out - no new fetch is started and the timer is not
  // re-armed.
  EXPECT_CALL(*timer_, enableTimer(_, _)).Times(0);
  timer_->invokeCallback();
  EXPECT_EQ(fetch_receiver_array_.size(), 1);
}

// A fetcher constructed while its filter chain is already draining must bail out of the initial
// fetch(): no fetch is started and no timer is armed.
TEST_P(JwksAsyncFetcherTest, TestNoInitialFetchWhenConstructedWhileDraining) {
  const char config[] = R"(
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
      async_fetch: {}
)";

  // The filter chain is already draining before the fetcher is constructed. The initial fetch
  // triggered from the constructor (fast_listener) or the init target must bail out.
  ON_CALL(context_.drain_manager_, drainClose(_)).WillByDefault(testing::Return(true));
  setupAsyncFetcher(config);

  EXPECT_EQ(fetch_receiver_array_.size(), 0);
  EXPECT_FALSE(timer_->enabled());
  EXPECT_EQ(0U, stats_.jwks_fetch_success_.value());
  EXPECT_EQ(0U, stats_.jwks_fetch_failed_.value());
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
