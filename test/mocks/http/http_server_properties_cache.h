#pragma once
#include "envoy/http/http_server_properties_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockHttpServerPropertiesCache : public HttpServerPropertiesCache {
public:
  ~MockHttpServerPropertiesCache() override;

  MOCK_METHOD(void, setAlternatives,
              (const Origin& origin, std::vector<AlternateProtocol>& protocols));
  MOCK_METHOD(void, setSrtt, (const Origin& origin, std::chrono::microseconds srtt));
  MOCK_METHOD(std::chrono::microseconds, getSrtt, (const Origin& origin), (const));
  MOCK_METHOD(void, setConcurrentStreams, (const Origin& origin, uint32_t concurrent_streams));
  MOCK_METHOD(uint32_t, getConcurrentStreams, (const Origin& origin), (const));
  MOCK_METHOD(OptRef<const std::vector<AlternateProtocol>>, findAlternatives,
              (const Origin& origin));
  MOCK_METHOD(size_t, size, (), (const));
  MOCK_METHOD(HttpServerPropertiesCache::Http3StatusTracker&, getOrCreateHttp3StatusTracker,
              (const Origin& origin));
  MOCK_METHOD(void, resetBrokenness, ());
};

class MockHttpServerPropertiesCacheManager : public HttpServerPropertiesCacheManager {
public:
  ~MockHttpServerPropertiesCacheManager() override;

  MOCK_METHOD(HttpServerPropertiesCacheSharedPtr, getCache,
              (const envoy::config::core::v3::AlternateProtocolsCacheOptions& config,
               Event::Dispatcher& dispatcher));
  MOCK_METHOD(void, forEachThreadLocalCache, (HttpServerPropertiesCacheManager::CacheFn));
};

} // namespace Http
} // namespace Envoy
