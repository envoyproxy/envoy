#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/http/cache/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/http_cache_implementation_test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class SimpleHttpCacheTestDelegate : public HttpCacheTestDelegate {
public:
  std::shared_ptr<HttpCache> cache() override { return cache_; }
  bool validationEnabled() const override { return true; }

private:
  std::shared_ptr<SimpleHttpCache> cache_ = std::make_shared<SimpleHttpCache>();
};

INSTANTIATE_TEST_SUITE_P(SimpleHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<SimpleHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "SimpleHttpCache";
                         });

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.http.cache.simple_http_cache.v3.SimpleHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3::CacheConfig config;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  config.mutable_typed_config()->PackFrom(*factory->createEmptyConfigProto());
  EXPECT_EQ(factory->getCache(config, factory_context)->cacheInfo().name_,
            "envoy.extensions.http.cache.simple");
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
