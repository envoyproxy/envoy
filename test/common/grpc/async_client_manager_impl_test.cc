#include <chrono>
#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/async_client_manager_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using envoy::config::bootstrap::v3::Bootstrap;
using ::testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class RawAsyncClientCacheTest : public testing::Test {
public:
  RawAsyncClientCacheTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  // advanceTimeAndRun moves the current time as requested, and then executes
  // all executable timers in a non-deterministic order. This mimics real-time behavior in
  // libevent if there is a long delay between libevent regaining control. Here we want to
  // test behavior with a specific sequence of events, where each timer fires within a
  // simulated second of what was programmed.
  void waitForSeconds(int seconds) {
    for (int i = 0; i < seconds; i++) {
      time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                     Event::Dispatcher::RunType::NonBlock);
    }
  }

  void initialize(std::chrono::milliseconds entry_timeout_interval) {
    client_cache_ = std::make_unique<AsyncClientManagerImpl::RawAsyncClientCache>(
        *dispatcher_, entry_timeout_interval);
  }

protected:
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<AsyncClientManagerImpl::RawAsyncClientCache> client_cache_;
};

// Test cache eviction time with 50000ms.
TEST_F(RawAsyncClientCacheTest, CacheEvictionMilliseconds) {
  initialize(std::chrono::milliseconds(50000));
  envoy::config::core::v3::GrpcService foo_service;
  foo_service.mutable_envoy_grpc()->set_cluster_name("foo");
  GrpcServiceConfigWithHashKey config_with_hash_key(foo_service);
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  client_cache_->setCache(config_with_hash_key, foo_client);
  waitForSeconds(49);
  // Cache entry hasn't been evicted because it was created 49s ago.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  waitForSeconds(49);
  // Cache entry hasn't been evicted because it was accessed 49s ago.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  waitForSeconds(51);
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), nullptr);
}

// Test cache eviction time with 20s.
TEST_F(RawAsyncClientCacheTest, CacheEvictionWithSeconds) {
  initialize(std::chrono::seconds(20));
  envoy::config::core::v3::GrpcService foo_service;
  foo_service.mutable_envoy_grpc()->set_cluster_name("foo");
  GrpcServiceConfigWithHashKey config_with_hash_key(foo_service);
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  client_cache_->setCache(config_with_hash_key, foo_client);
  waitForSeconds(19);
  // Cache entry hasn't been evicted because it was created 19s ago.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  waitForSeconds(19);
  // Cache entry hasn't been evicted because it was accessed 19s ago.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  waitForSeconds(21);
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), nullptr);
}

// Test multiple cache entries eviction behaviour.
TEST_F(RawAsyncClientCacheTest, MultipleCacheEntriesEviction) {
  initialize(std::chrono::milliseconds(50000));
  envoy::config::core::v3::GrpcService grpc_service;
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  for (int i = 1; i <= 50; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    GrpcServiceConfigWithHashKey config_with_hash_key(grpc_service);
    client_cache_->setCache(config_with_hash_key, foo_client);
  }
  waitForSeconds(20);
  for (int i = 51; i <= 100; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    GrpcServiceConfigWithHashKey config_with_hash_key(grpc_service);
    client_cache_->setCache(config_with_hash_key, foo_client);
  }
  waitForSeconds(30);
  // Cache entries created 50s before have expired.
  for (int i = 1; i <= 50; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    GrpcServiceConfigWithHashKey config_with_hash_key(grpc_service);
    EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), nullptr);
  }
  // Cache entries 30s before haven't expired.
  for (int i = 51; i <= 100; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    GrpcServiceConfigWithHashKey config_with_hash_key(grpc_service);
    EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  }
}

// Test the case when the eviction timer doesn't fire on time, getting the oldest entry that has
// already expired but hasn't been evicted should succeed.
TEST_F(RawAsyncClientCacheTest, GetExpiredButNotEvictedCacheEntry) {
  initialize(std::chrono::milliseconds(50000));
  envoy::config::core::v3::GrpcService foo_service;
  foo_service.mutable_envoy_grpc()->set_cluster_name("foo");
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  GrpcServiceConfigWithHashKey config_with_hash_key(foo_service);
  client_cache_->setCache(config_with_hash_key, foo_client);
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(50));
  // Cache entry hasn't been evicted because it is accessed before timer fire.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), foo_client.get());
  time_system_.advanceTimeAndRun(std::chrono::seconds(50), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  // Cache entry has been evicted because it is accessed after timer fire.
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key).get(), nullptr);
}

class RawAsyncClientCacheTestBusyLoop : public testing::Test {
public:
  RawAsyncClientCacheTestBusyLoop() {
    timer_ = new Event::MockTimer();
    EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Invoke([this](Event::TimerCb) {
      return timer_;
    }));
    client_cache_ = std::make_unique<AsyncClientManagerImpl::RawAsyncClientCache>(
        dispatcher_, std::chrono::milliseconds(50000));
    EXPECT_CALL(*timer_, enableTimer(testing::Not(std::chrono::milliseconds(0)), _))
        .Times(testing::AtLeast(1));
  }

  void waitForMilliSeconds(int ms) {
    for (int i = 0; i < ms; i++) {
      time_system_.advanceTimeAndRun(std::chrono::milliseconds(1), dispatcher_,
                                     Event::Dispatcher::RunType::NonBlock);
    }
  }

protected:
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<AsyncClientManagerImpl::RawAsyncClientCache> client_cache_;
};

TEST_F(RawAsyncClientCacheTestBusyLoop, MultipleCacheEntriesEvictionBusyLoop) {
  envoy::config::core::v3::GrpcService grpc_service;
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  // two entries are added to the cache
  for (int i = 1; i <= 2; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    GrpcServiceConfigWithHashKey config_with_hash_key(grpc_service);
    client_cache_->setCache(config_with_hash_key, foo_client);
  }
  // waiting for 49.2 secs to make sure that for the entry which is not accessed, time to expire is
  // less than 1 second, ~0.8 secs
  waitForMilliSeconds(49200);

  // Access first cache entry to so that evictEntriesAndResetEvictionTimer() gets called.
  // Since we are getting first entry, access time of first entry will be updated to current time.
  grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(1));
  GrpcServiceConfigWithHashKey config_with_hash_key_1(grpc_service);
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key_1).get(), foo_client.get());

  // Verifying that though the time to expire for second entry ~0.8 sec, it is considered as expired
  // to avoid the busy loop which could happen if timer gets enabled with 0(0.8 rounded off to 0)
  // duration.
  grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(2));
  GrpcServiceConfigWithHashKey config_with_hash_key_2(grpc_service);
  EXPECT_EQ(client_cache_->getCache(config_with_hash_key_2).get(), nullptr);
}

class AsyncClientManagerImplTest : public testing::Test {
public:
  AsyncClientManagerImplTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_grpc_manager")),
        stat_names_(scope_.symbolTable()) {
    context_.thread_local_.setDispatcher(dispatcher_.get());
  }

  void initialize(absl::optional<Bootstrap::GrpcAsyncClientManagerConfig> config = absl::nullopt) {
    ON_CALL(context_, clusterManager()).WillByDefault(testing::ReturnRef(cm_));
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(testing::ReturnRef(*dispatcher_));
    ON_CALL(context_, timeSource()).WillByDefault(testing::ReturnRef(time_system_));
    ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));
    if (config.has_value()) {
      async_client_manager_ = std::make_unique<AsyncClientManagerImpl>(
          cm_, context_.threadLocal(), context_, stat_names_, config.value());
    } else {
      async_client_manager_ = std::make_unique<AsyncClientManagerImpl>(
          cm_, context_.threadLocal(), context_, stat_names_,
          Bootstrap::GrpcAsyncClientManagerConfig());
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Upstream::MockClusterManager cm_;
  Stats::MockStore store_;
  Stats::MockScope& scope_{store_.mockScope()};
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  StatNames stat_names_;
  std::unique_ptr<AsyncClientManagerImpl> async_client_manager_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  initialize();
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  EXPECT_CALL(cm_, checkActiveStaticCluster("foo")).WillOnce(Return(absl::OkStatus()));
  ASSERT_TRUE(async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).ok());
}

TEST_F(AsyncClientManagerImplTest, GrpcServiceConfigWithHashKeyTest) {
  initialize();
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  envoy::config::core::v3::GrpcService grpc_service_c;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("bar");

  GrpcServiceConfigWithHashKey config_with_hash_key_a = GrpcServiceConfigWithHashKey(grpc_service);
  GrpcServiceConfigWithHashKey config_with_hash_key_b = GrpcServiceConfigWithHashKey(grpc_service);
  GrpcServiceConfigWithHashKey config_with_hash_key_c =
      GrpcServiceConfigWithHashKey(grpc_service_c);
  EXPECT_TRUE(config_with_hash_key_a == config_with_hash_key_b);
  EXPECT_FALSE(config_with_hash_key_a == config_with_hash_key_c);

  EXPECT_EQ(config_with_hash_key_a.getPreComputedHash(),
            config_with_hash_key_b.getPreComputedHash());
  EXPECT_NE(config_with_hash_key_a.getPreComputedHash(),
            config_with_hash_key_c.getPreComputedHash());
}

// Test the client cache time is correctly configured with seconds from the bootstrap proto.
TEST_F(AsyncClientManagerImplTest, RawAsyncClientCacheWithSecondsConfig) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Bootstrap::GrpcAsyncClientManagerConfig aync_manager_config;
  aync_manager_config.mutable_max_cached_entry_idle_duration()->MergeFrom(
      ProtobufUtil::TimeUtil::SecondsToDuration(20));

  initialize(aync_manager_config);

  RawAsyncClientSharedPtr foo_client_0 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  RawAsyncClientSharedPtr foo_client_1 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_EQ(foo_client_0.get(), foo_client_1.get());

  time_system_.advanceTimeAndRun(std::chrono::seconds(19), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);

  RawAsyncClientSharedPtr foo_client_2 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_EQ(foo_client_1.get(), foo_client_2.get());

  // Here we want to test behavior with a specific sequence of events, where each timer
  // fires within a simulated second of what was programmed.
  for (int i = 0; i < 21; i++) {
    time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
  }

  RawAsyncClientSharedPtr foo_client_3 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_NE(foo_client_2.get(), foo_client_3.get());
}

// Test the client cache time is correctly configured with milliseconds from the bootstrap proto.
TEST_F(AsyncClientManagerImplTest, RawAsyncClientCacheWithMilliConfig) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  Bootstrap::GrpcAsyncClientManagerConfig aync_manager_config;
  aync_manager_config.mutable_max_cached_entry_idle_duration()->MergeFrom(
      ProtobufUtil::TimeUtil::MillisecondsToDuration(30000));

  initialize(aync_manager_config);

  RawAsyncClientSharedPtr foo_client_0 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  RawAsyncClientSharedPtr foo_client_1 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_EQ(foo_client_0.get(), foo_client_1.get());

  time_system_.advanceTimeAndRun(std::chrono::milliseconds(29999), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);

  RawAsyncClientSharedPtr foo_client_2 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_EQ(foo_client_1.get(), foo_client_2.get());

  // Here we want to test behavior with a specific sequence of events, where each timer
  // fires within a simulated second of what was programmed.
  for (int i = 0; i < 31; i++) {
    time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);
  }

  RawAsyncClientSharedPtr foo_client_3 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_NE(foo_client_2.get(), foo_client_3.get());
}

TEST_F(AsyncClientManagerImplTest, RawAsyncClientCache) {
  initialize();
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  // Use cache when runtime is enabled.
  RawAsyncClientSharedPtr foo_client0 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  RawAsyncClientSharedPtr foo_client1 =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_EQ(foo_client0.get(), foo_client1.get());

  // Get a different raw async client with different cluster config.
  grpc_service.mutable_envoy_grpc()->set_cluster_name("bar");
  RawAsyncClientSharedPtr bar_client =
      async_client_manager_->getOrCreateRawAsyncClient(grpc_service, scope_, true).value();
  EXPECT_NE(foo_client1.get(), bar_client.get());
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcInvalid) {
  initialize();
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  EXPECT_CALL(cm_, checkActiveStaticCluster("foo")).WillOnce(Invoke([](const std::string&) {
    return absl::InvalidArgumentError("failure");
  }));
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "failure");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  initialize();
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr,
            async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).value());
#else
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInKey) {
  initialize();
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("illegalcharacter;");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Illegal characters in gRPC initial metadata header key: illegalcharacter;.");
#else
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, LegalGoogleGrpcChar) {
  initialize();
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("_legal-character.");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr,
            async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).value());
#else
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInValue) {
  initialize();
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("legal-key");
  metadata.set_value("NonAsciValue.भारत");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Illegal ASCII value for gRPC initial metadata header key: legal-key.");
#else
  EXPECT_EQ(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, false).status().message(),
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknownSkipClusterCheck) {
  initialize();
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, checkActiveStaticCluster(_)).Times(0);
  ASSERT_TRUE(
      async_client_manager_->factoryForGrpcService(grpc_service, scope_, true).status().ok());
}

} // namespace
} // namespace Grpc
} // namespace Envoy
