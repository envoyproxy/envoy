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
  MOCK_METHOD(OptRef<const std::vector<AlternateProtocol>>, findAlternatives,
              (const Origin& origin));
  MOCK_METHOD(size_t, size, (), (const));
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
