#include "common/grpc/google_async_client_cache.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Grpc {
namespace {

class AsyncClientCacheTest : public testing::Test {
public:
  AsyncClientCacheTest() {
    client_cache_singleton_ = std::make_unique<AsyncClientCacheSingleton>();
  }

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, true))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  void expectCacheAndClientEqual(const AsyncClientCacheSharedPtr& expected_client_cache,
                                 const RawAsyncClientSharedPtr& expected_client,
                                 const ::envoy::config::core::v3::GrpcService config) {
    AsyncClientCacheSharedPtr actual_client_cache =
        client_cache_singleton_->getOrCreateAsyncClientCache(async_client_manager_, scope_, tls_,
                                                             config);
    EXPECT_EQ(expected_client_cache, actual_client_cache);
    EXPECT_EQ(expected_client, actual_client_cache->getAsyncClient());
  }

  void expectCacheAndClientNotEqual(const AsyncClientCacheSharedPtr& expected_client_cache,
                                    const RawAsyncClientSharedPtr& expected_client,
                                    const ::envoy::config::core::v3::GrpcService config) {
    AsyncClientCacheSharedPtr actual_client_cache =
        client_cache_singleton_->getOrCreateAsyncClientCache(async_client_manager_, scope_, tls_,
                                                             config);
    EXPECT_NE(expected_client_cache, actual_client_cache);
    EXPECT_NE(expected_client, actual_client_cache->getAsyncClient());
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Grpc::MockAsyncClientManager async_client_manager_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncClientFactory* factory_ = nullptr;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  std::unique_ptr<AsyncClientCacheSingleton> client_cache_singleton_;
  std::unique_ptr<AsyncClientCache> client_cache_;
};

TEST_F(AsyncClientCacheTest, Deduplication) {
  Stats::IsolatedStoreImpl scope;
  testing::InSequence s;

  ::envoy::config::core::v3::GrpcService config;
  config.mutable_google_grpc()->set_target_uri("dns://test01");
  config.mutable_google_grpc()->set_credentials_factory_name("test_credential01");

  expectClientCreation();
  AsyncClientCacheSharedPtr test_client_cache_01 =
      client_cache_singleton_->getOrCreateAsyncClientCache(async_client_manager_, scope_, tls_,
                                                           config);
  RawAsyncClientSharedPtr test_client_01 = test_client_cache_01->getAsyncClient();
  // Fetches the existing client and they should be equal.
  expectCacheAndClientEqual(test_client_cache_01, test_client_01, config);

  config.mutable_google_grpc()->set_credentials_factory_name("test_credential02");
  expectClientCreation();
  // Different credentials use different clients.
  expectCacheAndClientNotEqual(test_client_cache_01, test_client_01, config);

  AsyncClientCacheSharedPtr test_client_cache_02 =
      client_cache_singleton_->getOrCreateAsyncClientCache(async_client_manager_, scope_, tls_,
                                                           config);
  RawAsyncClientSharedPtr test_client_02 = test_client_cache_02->getAsyncClient();

  config.mutable_google_grpc()->set_credentials_factory_name("test_credential02");
  // No creation, fetching the existing one.
  expectCacheAndClientEqual(test_client_cache_02, test_client_02, config);

  // Different targets use different clients.
  config.mutable_google_grpc()->set_target_uri("dns://test02");
  expectClientCreation();
  expectCacheAndClientNotEqual(test_client_cache_01, test_client_01, config);
  expectCacheAndClientNotEqual(test_client_cache_02, test_client_02, config);
}

} // namespace
} // namespace Grpc
} // namespace Envoy
