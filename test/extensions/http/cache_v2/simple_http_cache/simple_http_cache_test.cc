#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_sessions.h"
#include "source/extensions/http/cache_v2/simple_http_cache/simple_http_cache.h"

#include "test/extensions/filters/http/cache_v2/http_cache_implementation_test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

class SimpleHttpCacheTestDelegate : public HttpCacheTestDelegate {
public:
  HttpCache& cache() override { return cache_; }

private:
  SimpleHttpCache cache_;
};

INSTANTIATE_TEST_SUITE_P(SimpleHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<SimpleHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "SimpleHttpCache";
                         });

TEST(Registration, GetFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.http.cache_v2.simple_http_cache.v3.SimpleHttpCacheV2Config");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache_v2::v3::CacheV2Config config;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  config.mutable_typed_config()->PackFrom(*factory->createEmptyConfigProto());
  auto cache = factory->getCache(config, factory_context);
  ASSERT_OK(cache);
  EXPECT_EQ((*cache)->cacheInfo().name_, "envoy.extensions.http.cache_v2.simple");
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
