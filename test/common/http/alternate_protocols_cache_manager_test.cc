#include "source/common/http/alternate_protocols_cache_manager_impl.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Http {

namespace {
class AlternateProtocolsCacheManagerTest : public testing::Test,
                                           public Event::TestUsingSimulatedTime {
public:
  AlternateProtocolsCacheManagerTest() {
    options1_.set_name(name1_);
    options1_.mutable_max_entries()->set_value(max_entries1_);

    options2_.set_name(name2_);
    options2_.mutable_max_entries()->set_value(max_entries2_);
  }
  void initialize() {
    AlternateProtocolsData data(context_);
    factory_ = std::make_unique<Http::AlternateProtocolsCacheManagerFactoryImpl>(singleton_manager_,
                                                                                 tls_, data);
    manager_ = factory_->get();
  }

  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Http::AlternateProtocolsCacheManagerFactoryImpl> factory_;
  AlternateProtocolsCacheManagerSharedPtr manager_;
  const std::string name1_ = "name1";
  const std::string name2_ = "name2";
  const int max_entries1_ = 10;
  const int max_entries2_ = 20;
  Event::MockDispatcher dispatcher_;

  envoy::config::core::v3::AlternateProtocolsCacheOptions options1_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions options2_;
};

TEST_F(AlternateProtocolsCacheManagerTest, FactoryGet) {
  initialize();

  EXPECT_NE(nullptr, manager_);
  EXPECT_EQ(manager_, factory_->get());
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCache) {
  initialize();
  AlternateProtocolsCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheWithFlushingAndConcurrency) {
  EXPECT_CALL(context_.options_, concurrency()).WillOnce(Return(5));
  options1_.mutable_key_value_store_config();
  initialize();
  EXPECT_THROW_WITH_REGEX(manager_->getCache(options1_, dispatcher_), EnvoyException,
                          "options has key value store but Envoy has concurrency = 5");
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheForDifferentOptions) {
  initialize();
  AlternateProtocolsCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  AlternateProtocolsCacheSharedPtr cache2 = manager_->getCache(options2_, dispatcher_);
  EXPECT_NE(nullptr, cache2);
  EXPECT_NE(cache1, cache2);
}

TEST_F(AlternateProtocolsCacheManagerTest, GetCacheForConflictingOptions) {
  initialize();
  AlternateProtocolsCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  options2_.set_name(options1_.name());
  EXPECT_THROW_WITH_REGEX(
      manager_->getCache(options2_, dispatcher_), EnvoyException,
      "options specified alternate protocols cache 'name1' with different settings.*");
}

} // namespace
} // namespace Http
} // namespace Envoy
