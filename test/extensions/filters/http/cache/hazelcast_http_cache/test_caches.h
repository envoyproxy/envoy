#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/util.h"

#include "test/extensions/filters/http/cache/hazelcast_http_cache/test_accessors.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Base testable cache for both local and remote ones.
 *
 * Exposes accessor to extend testing flexibility. Storage control can be achieved
 * directly via test accessors.
 */
class HazelcastHttpTestCache : public HazelcastHttpCache {
public:
  HazelcastHttpTestCache(HazelcastHttpCacheConfig&& typed_config, const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& cache_config) :
    HazelcastHttpCache(std::move(typed_config), cache_config) {}

  TestAccessor& getTestAccessor() { return dynamic_cast<TestAccessor&>(*accessor_); }
};

/**
 * Testable cache with RemoteTestAccessor.
 *
 * Requires a running Hazelcast instance to be tested.
 */
class HazelcastRemoteTestCache : public HazelcastHttpTestCache {
public:
  HazelcastRemoteTestCache(HazelcastHttpCacheConfig&& typed_config, const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& cache_config) :
    HazelcastHttpTestCache(std::move(typed_config), cache_config) {}

  void start() override {
    if (accessor_ && accessor_->isRunning()) {
      return;
    }

    ClientConfig client_config = ConfigUtil::getClientConfig(cache_config_);
    client_config.getSerializationConfig().addDataSerializableFactory(
        HazelcastCacheEntrySerializableFactory::FACTORY_ID,
        boost::shared_ptr<DataSerializableFactory>(new HazelcastCacheEntrySerializableFactory()));

    if (!accessor_) {
      accessor_ = std::make_unique<RemoteTestAccessor>(
          *this, std::move(client_config), cache_config_.app_prefix(), body_partition_size_);
    }

    try {
      accessor_->connect();
    } catch (...) {
      throw EnvoyException("Hazelcast Client could not connect to any cluster.");
    }
  }
};

/**
 * Testable cache with LocalTestAccessor.
 *
 * Does not require a running Hazelcast instance. Instead, tests the cache
 * with local storage. This is the way the cache is tested in CI environment.
 */
class HazelcastLocalTestCache : public HazelcastHttpTestCache {
public:
  HazelcastLocalTestCache(HazelcastHttpCacheConfig&& typed_config, const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& cache_config) :
    HazelcastHttpTestCache(std::move(typed_config), cache_config) {}

  void start() override {
    if (!accessor_) {
      accessor_ = std::make_unique<LocalTestAccessor>();
    }
    accessor_->connect();
  }
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
