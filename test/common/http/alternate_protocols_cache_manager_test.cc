#include "source/common/http/alternate_protocols_cache_manager_impl.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

namespace {
class AlternateProtocolsCacheManagerTest : public testing::Test,
                                           public Event::TestUsingSimulatedTime {
public:
  AlternateProtocolsCacheManagerTest()
      : factory_(singleton_manager_, simTime(), tls_), manager_(factory_.get()) {
    options1_.set_name(name1_);
    options1_.mutable_max_entries()->set_value(max_entries1_);

    options2_.set_name(name2_);
    options2_.mutable_max_entries()->set_value(max_entries2_);
  }

  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Http::AlternateProtocolsCacheManagerFactoryImpl factory_;
  AlternateProtocolsCacheManagerSharedPtr manager_;
  const std::string name1_ = "name1";
  const std::string name2_ = "name2";
  const int max_entries1_ = 10;
  const int max_entries2_ = 20;

  envoy::config::core::v3::AlternateProtocolsCacheOptions options1_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions options2_;
};

TEST_F(AlternateProtocolsCacheManagerTest, FactoryGet) {
  EXPECT_NE(nullptr, manager_);
  EXPECT_EQ(manager_, factory_.get());
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCache) {
  AlternateProtocolsCacheSharedPtr cache = manager_->getCache(options1_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_));
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheForDifferentOptions) {
  AlternateProtocolsCacheSharedPtr cache1 = manager_->getCache(options1_);
  AlternateProtocolsCacheSharedPtr cache2 = manager_->getCache(options2_);
  EXPECT_NE(nullptr, cache2);
  EXPECT_NE(cache1, cache2);
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheForConflictingOptions) {
  AlternateProtocolsCacheSharedPtr cache1 = manager_->getCache(options1_);
  options2_.set_name(options1_.name());
  EXPECT_THROW_WITH_REGEX(
      manager_->getCache(options2_), EnvoyException,
      "options specified alternate protocols cache 'name1' with different settings.*");
}

} // namespace
} // namespace Http
} // namespace Envoy
