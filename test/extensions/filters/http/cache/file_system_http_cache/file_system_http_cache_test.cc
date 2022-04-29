#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/filesystem/directory.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/filters/http/cache/file_system_http_cache/file_system_http_cache.h"

#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/http_cache_implementation_test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {
namespace {

absl::string_view yaml_config = R"(
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig
    manager_config:
      thread_pool:
        thread_count: 1
    cache_path: /tmp/
)";

class FileSystemHttpCacheTestDelegate : public HttpCacheTestDelegate {
public:
  FileSystemHttpCacheTestDelegate() {
    envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
    TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
    ConfigProto cfg;
    MessageUtil::unpackTo(cache_config.typed_config(), cfg);
    cfg.set_cache_path(absl::StrCat(env_.temporaryDirectory(), "/"));
    deleteCacheFiles(cfg.cache_path());
    cache_config.mutable_typed_config()->PackFrom(cfg);
    const std::string type{
        TypeUtil::typeUrlToDescriptorFullName(cache_config.typed_config().type_url())};
    HttpCacheFactory* const http_cache_factory =
        Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
    if (http_cache_factory == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for type: '{}'", type));
    }

    cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
        http_cache_factory->getCache(cache_config, context_));
  }
  void deleteCacheFiles(std::string path) {
    for (const auto& it : ::Envoy::Filesystem::Directory(path)) {
      if (absl::StartsWith(it.name_, "cache-")) {
        env_.removePath(absl::StrCat(path, it.name_));
      }
    }
  }
  std::shared_ptr<HttpCache> cache() override { return cache_; }
  bool validationEnabled() const override { return true; }

private:
  ::Envoy::TestEnvironment env_;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<FileSystemHttpCache> cache_;
  LogLevelSetter log_level_ = LogLevelSetter(spdlog::level::debug);
};

INSTANTIATE_TEST_SUITE_P(FileSystemHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<FileSystemHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "FileSystemHttpCache";
                         });

TEST(Registration, GetCacheFromFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.filters.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
  testing::NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
  EXPECT_EQ(factory->getCache(cache_config, factory_context)->cacheInfo().name_,
            "envoy.extensions.filters.http.cache.file_system_http_cache");
}

} // namespace
} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
