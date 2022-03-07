#pragma once
#include "envoy/http/alternate_protocols_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockAlternateProtocolsCache : public AlternateProtocolsCache {
public:
  ~MockAlternateProtocolsCache() override;

  MOCK_METHOD(void, setAlternatives,
              (const Origin& origin, std::vector<AlternateProtocol>& protocols));
  MOCK_METHOD(void, setSrtt, (const Origin& origin, std::chrono::microseconds srtt));
  MOCK_METHOD(std::chrono::microseconds, getSrtt, (const Origin& origin), (const));
  MOCK_METHOD(OptRef<const std::vector<AlternateProtocol>>, findAlternatives,
              (const Origin& origin));
  MOCK_METHOD(size_t, size, (), (const));
  MOCK_METHOD(std::unique_ptr<Http3StatusTracker>, acquireHttp3StatusTracker,
              (const Origin& origin));
  MOCK_METHOD(void, storeHttp3StatusTracker,
              (const Origin& origin, std::unique_ptr<Http3StatusTracker> h3_status_tracker));
};

class MockAlternateProtocolsCacheManager : public AlternateProtocolsCacheManager {
public:
  ~MockAlternateProtocolsCacheManager() override;

  MOCK_METHOD(AlternateProtocolsCacheSharedPtr, getCache,
              (const envoy::config::core::v3::AlternateProtocolsCacheOptions& config,
               Event::Dispatcher& dispatcher));
};

class MockAlternateProtocolsCacheManagerFactory : public AlternateProtocolsCacheManagerFactory {
public:
  ~MockAlternateProtocolsCacheManagerFactory() override;

  MOCK_METHOD(AlternateProtocolsCacheManagerSharedPtr, get, ());
};

} // namespace Http
} // namespace Envoy
