#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/api/api_impl.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/grpc/async_client_manager_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Return;

namespace Envoy {
namespace Grpc {
namespace {

class RawAsyncClientCacheTest : public testing::Test {
public:
  RawAsyncClientCacheTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), client_cache_(*dispatcher_) {}

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

protected:
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  AsyncClientManagerImpl::RawAsyncClientCache client_cache_;
};

TEST_F(RawAsyncClientCacheTest, CacheEviction) {
  envoy::config::core::v3::GrpcService foo_service;
  foo_service.mutable_envoy_grpc()->set_cluster_name("foo");
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  client_cache_.setCache(foo_service, foo_client);
  waitForSeconds(49);
  // Cache entry hasn't been evicted because it was created 49s ago.
  EXPECT_EQ(client_cache_.getCache(foo_service).get(), foo_client.get());
  waitForSeconds(49);
  // Cache entry hasn't been evicted because it was accessed 49s ago.
  EXPECT_EQ(client_cache_.getCache(foo_service).get(), foo_client.get());
  waitForSeconds(51);
  EXPECT_EQ(client_cache_.getCache(foo_service).get(), nullptr);
}

TEST_F(RawAsyncClientCacheTest, MultipleCacheEntriesEviction) {
  envoy::config::core::v3::GrpcService grpc_service;
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  for (int i = 1; i <= 50; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    client_cache_.setCache(grpc_service, foo_client);
  }
  waitForSeconds(20);
  for (int i = 51; i <= 100; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    client_cache_.setCache(grpc_service, foo_client);
  }
  waitForSeconds(30);
  // Cache entries created 50s before have expired.
  for (int i = 1; i <= 50; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    EXPECT_EQ(client_cache_.getCache(grpc_service).get(), nullptr);
  }
  // Cache entries 30s before haven't expired.
  for (int i = 51; i <= 100; i++) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(std::to_string(i));
    EXPECT_EQ(client_cache_.getCache(grpc_service).get(), foo_client.get());
  }
}

// Test the case when the eviction timer doesn't fire on time, getting the oldest entry that has
// already expired but hasn't been evicted should succeed.
TEST_F(RawAsyncClientCacheTest, GetExpiredButNotEvictedCacheEntry) {
  envoy::config::core::v3::GrpcService foo_service;
  foo_service.mutable_envoy_grpc()->set_cluster_name("foo");
  RawAsyncClientSharedPtr foo_client = std::make_shared<MockAsyncClient>();
  client_cache_.setCache(foo_service, foo_client);
  time_system_.advanceTimeAsyncImpl(std::chrono::seconds(50));
  // Cache entry hasn't been evicted because it is accessed before timer fire.
  EXPECT_EQ(client_cache_.getCache(foo_service).get(), foo_client.get());
  time_system_.advanceTimeAndRun(std::chrono::seconds(50), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);
  // Cache entry has been evicted because it is accessed after timer fire.
  EXPECT_EQ(client_cache_.getCache(foo_service).get(), nullptr);
}

class AsyncClientManagerImplTest : public testing::Test {
public:
  AsyncClientManagerImplTest()
      : api_(Api::createApiForTest()), stat_names_(scope_.symbolTable()),
        async_client_manager_(cm_, tls_, test_time_.timeSystem(), *api_, stat_names_) {}

  Upstream::MockClusterManager cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::MockStore scope_;
  DangerousDeprecatedTestTime test_time_;
  Api::ApiPtr api_;
  StatNames stat_names_;
  AsyncClientManagerImpl async_client_manager_;
};

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcOk) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  EXPECT_CALL(cm_, checkActiveStaticCluster("foo")).WillOnce(Return());
  async_client_manager_.factoryForGrpcService(grpc_service, scope_, false);
}

TEST_F(AsyncClientManagerImplTest, RawAsyncClientCache) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  // Use cache when runtime is enabled.
  RawAsyncClientSharedPtr foo_client0 =
      async_client_manager_.getOrCreateRawAsyncClient(grpc_service, scope_, true);
  RawAsyncClientSharedPtr foo_client1 =
      async_client_manager_.getOrCreateRawAsyncClient(grpc_service, scope_, true);
  EXPECT_EQ(foo_client0.get(), foo_client1.get());

  // Get a different raw async client with different cluster config.
  grpc_service.mutable_envoy_grpc()->set_cluster_name("bar");
  RawAsyncClientSharedPtr bar_client =
      async_client_manager_.getOrCreateRawAsyncClient(grpc_service, scope_, true);
  EXPECT_NE(foo_client1.get(), bar_client.get());
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcInvalid) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");
  EXPECT_CALL(cm_, checkActiveStaticCluster("foo")).WillOnce(Invoke([](const std::string&) {
    throw EnvoyException("fake exception");
  }));
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "fake exception");
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpc) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager_.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInKey) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("illegalcharacter;");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Illegal characters in gRPC initial metadata header key: illegalcharacter;.");
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, LegalGoogleGrpcChar) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("_legal-character.");
  metadata.set_value("value");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_NE(nullptr, async_client_manager_.factoryForGrpcService(grpc_service, scope_, false));
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, GoogleGrpcIllegalCharsInValue) {
  EXPECT_CALL(scope_, createScope_("grpc.foo."));
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_google_grpc()->set_stat_prefix("foo");

  auto& metadata = *grpc_service.mutable_initial_metadata()->Add();
  metadata.set_key("legal-key");
  metadata.set_value("NonAsciValue.भारत");

#ifdef ENVOY_GOOGLE_GRPC
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Illegal ASCII value for gRPC initial metadata header key: legal-key.");
#else
  EXPECT_THROW_WITH_MESSAGE(
      async_client_manager_.factoryForGrpcService(grpc_service, scope_, false), EnvoyException,
      "Google C++ gRPC client is not linked");
#endif
}

TEST_F(AsyncClientManagerImplTest, EnvoyGrpcUnknownSkipClusterCheck) {
  envoy::config::core::v3::GrpcService grpc_service;
  grpc_service.mutable_envoy_grpc()->set_cluster_name("foo");

  EXPECT_CALL(cm_, checkActiveStaticCluster(_)).Times(0);
  ASSERT_NO_THROW(async_client_manager_.factoryForGrpcService(grpc_service, scope_, true));
}

} // namespace
} // namespace Grpc
} // namespace Envoy
