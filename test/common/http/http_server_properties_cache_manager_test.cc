#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/singleton/manager_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

namespace {
class HttpServerPropertiesCacheManagerTest : public testing::Test,
                                             public Event::TestUsingSimulatedTime {
public:
  HttpServerPropertiesCacheManagerTest() {
    options1_.set_name(name1_);
    options1_.mutable_max_entries()->set_value(max_entries1_);

    options2_.set_name(name2_);
    options2_.mutable_max_entries()->set_value(max_entries2_);
  }
  void initialize() {
    AlternateProtocolsData data(context_);
    factory_ = std::make_unique<Http::HttpServerPropertiesCacheManagerFactoryImpl>(
        singleton_manager_, tls_, data);
    manager_ = factory_->get();
  }

  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  testing::NiceMock<ThreadLocal::MockInstance> tls_;
  std::unique_ptr<Http::HttpServerPropertiesCacheManagerFactoryImpl> factory_;
  HttpServerPropertiesCacheManagerSharedPtr manager_;
  const std::string name1_ = "name1";
  const std::string name2_ = "name2";
  const int max_entries1_ = 10;
  const int max_entries2_ = 20;
  Event::MockDispatcher dispatcher_;

  envoy::config::core::v3::AlternateProtocolsCacheOptions options1_;
  envoy::config::core::v3::AlternateProtocolsCacheOptions options2_;
};

TEST_F(HttpServerPropertiesCacheManagerTest, FactoryGet) {
  initialize();

  EXPECT_NE(nullptr, manager_);
  EXPECT_EQ(manager_, factory_->get());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCache) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithEntry) {
  auto* entry = options1_.add_prepopulated_entries();
  entry->set_hostname("foo.com");
  entry->set_port(1);

  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));

  const HttpServerPropertiesCacheImpl::Origin origin = {"https", entry->hostname(), entry->port()};
  EXPECT_TRUE(cache->findAlternatives(origin).has_value());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithInvalidCanonicalEntry) {
  auto* suffixes = options1_.add_canonical_suffixes();
  *suffixes = "example.com";

  initialize();
  EXPECT_ENVOY_BUG(manager_->getCache(options1_, dispatcher_),
                   "Suffix does not start with a leading '.': example.com");
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheWithCanonicalEntry) {
  auto* suffixes = options1_.add_canonical_suffixes();
  *suffixes = ".example.com";
  auto* entry = options1_.add_prepopulated_entries();
  entry->set_hostname("first.example.com");
  entry->set_port(1);

  initialize();
  HttpServerPropertiesCacheSharedPtr cache = manager_->getCache(options1_, dispatcher_);
  EXPECT_NE(nullptr, cache);
  EXPECT_EQ(cache, manager_->getCache(options1_, dispatcher_));

  const HttpServerPropertiesCacheImpl::Origin origin = {"https", "second.example.com",
                                                        entry->port()};
  EXPECT_TRUE(cache->findAlternatives(origin).has_value());
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheForDifferentOptions) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  HttpServerPropertiesCacheSharedPtr cache2 = manager_->getCache(options2_, dispatcher_);
  EXPECT_NE(nullptr, cache2);
  EXPECT_NE(cache1, cache2);
}

TEST_F(HttpServerPropertiesCacheManagerTest, GetCacheForConflictingOptions) {
  initialize();
  HttpServerPropertiesCacheSharedPtr cache1 = manager_->getCache(options1_, dispatcher_);
  options2_.set_name(options1_.name());
  EXPECT_ENVOY_BUG(manager_->getCache(options2_, dispatcher_),
                   "options specified alternate protocols cache 'name1' with different settings "
                   "first 'name: \"name1\"");
}

} // namespace
} // namespace Http
} // namespace Envoy
