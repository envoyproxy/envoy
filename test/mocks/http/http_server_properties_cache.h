#pragma once
#include "envoy/http/http_server_properties_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHttpServerPropertiesCache : public HttpServerPropertiesCache {
public:
  ~MockHttpServerPropertiesCache() override;

  MOCK_METHOD(void, setAlternatives,
              (const Origin& origin, std::vector<AlternateProtocol>& protocols), (override));
  MOCK_METHOD(void, setSrtt, (const Origin& origin, std::chrono::microseconds srtt), (override));
  MOCK_METHOD(std::chrono::microseconds, getSrtt, (const Origin& origin, bool use_canonical_suffix),
              (const, override));
  MOCK_METHOD(void, setConcurrentStreams, (const Origin& origin, uint32_t concurrent_streams),
              (override));
  MOCK_METHOD(uint32_t, getConcurrentStreams, (const Origin& origin), (const, override));
  MOCK_METHOD(OptRef<const std::vector<AlternateProtocol>>, findAlternatives,
              (const Origin& origin), (override));
  MOCK_METHOD(size_t, size, (), (const, override));
  MOCK_METHOD(HttpServerPropertiesCache::Http3StatusTracker&, getOrCreateHttp3StatusTracker,
              (const Origin& origin), (override));
  MOCK_METHOD(bool, isHttp3Broken, (const Origin& origin), (override));
  MOCK_METHOD(void, markHttp3Broken, (const Origin& origin), (override));
  MOCK_METHOD(void, resetBrokenness, (), (override));
  MOCK_METHOD(void, resetStatus, (), (override));
};

class MockHttpServerPropertiesCacheManager : public HttpServerPropertiesCacheManager {
public:
  ~MockHttpServerPropertiesCacheManager() override;

  MOCK_METHOD(HttpServerPropertiesCacheSharedPtr, getCache,
              (const envoy::config::core::v3::AlternateProtocolsCacheOptions& config,
               Event::Dispatcher& dispatcher),
              (override));
  MOCK_METHOD(void, forEachThreadLocalCache, (HttpServerPropertiesCacheManager::CacheFn),
              (override));
};

} // namespace Http
} // namespace Envoy
