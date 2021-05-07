#include "common/http/alternate_protocols_cache_manager_impl.h"
#include "common/singleton/manager_impl.h"

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
    config1_.set_name(name1_);
    config1_.mutable_max_hosts()->set_value(max_hosts1_);

    config1_.set_name(name2_);
    config1_.mutable_max_hosts()->set_value(max_hosts2_);
  }

  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  Http::AlternateProtocolsCacheManagerFactoryImpl factory_;
  AlternateProtocolsCacheManagerSharedPtr manager_;
  const std::string name1_ = "name1";
  const std::string name2_ = "name2";
  const int max_hosts1_ = 10;
  const int max_hosts2_ = 20;

  envoy::config::core::v3::AlternateProtocolsCacheOptions config1_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions config2_;
};

TEST_F(AlternateProtocolsCacheManagerTest, FactoryGet) {
  EXPECT_NE(nullptr, manager_);
  EXPECT_EQ(manager_, factory_.get());
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCache) {
  AlternateProtocolsCacheSharedPtr cache = manager_->getCache(config1_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(config1_));
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheForDifferentConfig) {
  AlternateProtocolsCacheSharedPtr cache1 = manager_->getCache(config1_);
  AlternateProtocolsCacheSharedPtr cache2 = manager_->getCache(config2_);
  EXPECT_NE(nullptr, cache2);
  EXPECT_NE(cache1, cache2);
}

} // namespace
} // namespace Http
} // namespace Envoy
