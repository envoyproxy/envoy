#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/singleton/manager_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/bandwidth_share/token_bucket_singleton.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace {

using testing::_;
using testing::NiceMock;

envoy::config::core::v3::RuntimeUInt32 runtimeConfig(const std::string& runtime_key,
                                                     uint32_t default_value) {
  envoy::config::core::v3::RuntimeUInt32 config;
  config.set_runtime_key(runtime_key);
  config.set_default_value(default_value);
  return config;
}

class TokenBucketSingletonTest : public testing::Test {
protected:
  Stats::Scope& scope() { return *store_.rootScope(); }

  Runtime::UInt32 runtimeUInt32(uint32_t default_value = 1) {
    return Runtime::UInt32(runtimeConfig("bandwidth.bucket.kbps", default_value), runtime_);
  }

  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(TokenBucketSingletonTest, GetUsesSingletonManager) {
  Singleton::ManagerImpl manager;

  auto first = TokenBucketSingleton::get(manager, time_system_, scope(), tls_);
  auto second = TokenBucketSingleton::get(manager, time_system_, scope(), tls_);

  EXPECT_EQ(first, second);
}

TEST_F(TokenBucketSingletonTest, ReturnsCachedBucketUntilRuntimeValueChanges) {
  uint64_t runtime_kbps = 1;
  ON_CALL(runtime_.snapshot_, getInteger(_, _))
      .WillByDefault([&runtime_kbps](absl::string_view, uint64_t) { return runtime_kbps; });
  TokenBucketSingleton singleton(time_system_, scope(), tls_);

  ASSERT_TRUE(
      singleton.setBucket("shared", runtimeUInt32(), 1, std::chrono::milliseconds{25}).ok());
  ASSERT_TRUE(
      singleton.setBucket("shared", runtimeUInt32(), 1, std::chrono::milliseconds{25}).ok());

  auto initial = singleton.getBucket("shared");
  ASSERT_NE(nullptr, initial);
  EXPECT_EQ(std::chrono::milliseconds{25}, initial->fillInterval());
  EXPECT_EQ(initial, singleton.getBucket("shared"));

  runtime_kbps = 2;
  auto updated = singleton.getBucket("shared");
  ASSERT_NE(nullptr, updated);
  EXPECT_NE(initial, updated);
  EXPECT_EQ(updated, singleton.getBucket("shared"));
}

TEST_F(TokenBucketSingletonTest, RuntimeValueChangeReleasesOldCachedBucket) {
  uint64_t runtime_kbps = 1;
  ON_CALL(runtime_.snapshot_, getInteger(_, _))
      .WillByDefault([&runtime_kbps](absl::string_view, uint64_t) { return runtime_kbps; });
  TokenBucketSingleton singleton(time_system_, scope(), tls_);

  ASSERT_TRUE(
      singleton.setBucket("shared", runtimeUInt32(), 1, std::chrono::milliseconds{25}).ok());

  std::shared_ptr<FairTokenBucket::Bucket> initial = singleton.getBucket("shared");
  std::weak_ptr<FairTokenBucket::Bucket> weak_initial = initial;
  initial.reset();
  ASSERT_FALSE(weak_initial.expired());

  runtime_kbps = 2;
  ASSERT_NE(nullptr, singleton.getBucket("shared"));
  EXPECT_TRUE(weak_initial.expired());
}

TEST_F(TokenBucketSingletonTest, RuntimeValueZeroDisablesBucketAndCanReenable) {
  uint64_t runtime_kbps = 0;
  ON_CALL(runtime_.snapshot_, getInteger(_, _))
      .WillByDefault([&runtime_kbps](absl::string_view, uint64_t) { return runtime_kbps; });
  TokenBucketSingleton singleton(time_system_, scope(), tls_);

  ASSERT_TRUE(
      singleton.setBucket("maybe", runtimeUInt32(10), 10, std::chrono::milliseconds{50}).ok());
  EXPECT_EQ(nullptr, singleton.getBucket("maybe"));

  runtime_kbps = 3;
  auto enabled = singleton.getBucket("maybe");
  ASSERT_NE(nullptr, enabled);

  runtime_kbps = 0;
  EXPECT_EQ(nullptr, singleton.getBucket("maybe"));
}

} // namespace
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
