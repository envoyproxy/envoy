#include "contrib/sip_proxy/filters/network/source/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

TEST(CacheManagerTest, FindTypeNotExist) {
  auto cache_manager = CacheManager<std::string, std::string, std::string>();
  auto result = cache_manager.at("empty", "empty");
  EXPECT_EQ(absl::nullopt, result);
  EXPECT_EQ(false, cache_manager.contains("empty", "empty"));
}

TEST(CacheManagerTest, FindKeyNotExist) {
  auto cache_manager = CacheManager<std::string, std::string, std::string>();
  cache_manager.initCache("lskpmc", 12);
  cache_manager.insertCache("lskpmc", "S3F2", "192.168.0.1");
  auto result = cache_manager.at("lskpmc", "fake");
  EXPECT_EQ(absl::nullopt, result);
  EXPECT_EQ(true, cache_manager.contains("lskpmc", "S3F2"));
  EXPECT_EQ(false, cache_manager.contains("lskpmc", "fake"));
}

TEST(CacheManagerTest, FindKeyExist) {
  auto cache_manager = CacheManager<std::string, std::string, std::string>();
  cache_manager.insertCache("lskpmc", "S3F2", "192.168.0.1");
  auto result = cache_manager.at("lskpmc", "S3F2");
  EXPECT_EQ("192.168.0.1", result.value().get());
  EXPECT_EQ(true, cache_manager.contains("lskpmc", "S3F2"));
}

TEST(CacheManagerTest, CacheSizeExceed) {
  auto cache_manager = CacheManager<std::string, std::string, std::string>();
  cache_manager.initCache("lskpmc", 2);
  cache_manager.insertCache("lskpmc", "S3F1", "192.168.0.1");
  cache_manager.insertCache("lskpmc", "S3F2", "192.168.0.1");
  cache_manager.insertCache("lskpmc", "S3F3", "192.168.0.1");

  EXPECT_EQ(2, cache_manager["lskpmc"].size());
}

TEST(CacheManagerTest, CacheReplacement) {
  auto cache_manager = CacheManager<std::string, std::string, std::string>();
  cache_manager.initCache("lskpmc", 2);
  cache_manager.insertCache("lskpmc", "S3F1", "192.168.0.1");
  cache_manager.insertCache("lskpmc", "S3F2", "192.168.0.1");
  cache_manager.insertCache("lskpmc", "S3F2", "192.168.0.2");

  EXPECT_EQ(2, cache_manager["lskpmc"].size());
  auto result = cache_manager.at("lskpmc", "S3F2");
  EXPECT_EQ("192.168.0.2", result.value().get());
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
