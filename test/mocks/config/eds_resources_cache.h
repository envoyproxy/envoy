#pragma once

#include "envoy/config/eds_resources_cache.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockEdsResourcesCache : public EdsResourcesCache {
public:
  MockEdsResourcesCache();
  ~MockEdsResourcesCache() override = default;

  MOCK_METHOD(void, setResource,
              (absl::string_view resource_name,
               const envoy::config::endpoint::v3::ClusterLoadAssignment& resource));
  MOCK_METHOD(void, removeResource, (absl::string_view resource_name));
  MOCK_METHOD(OptRef<const envoy::config::endpoint::v3::ClusterLoadAssignment>, getResource,
              (absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb));
  MOCK_METHOD(void, removeCallback,
              (absl::string_view resource_name, EdsResourceRemovalCallback* removal_cb));
  MOCK_METHOD(uint32_t, cacheSizeForTest, (), (const));

  MOCK_METHOD(void, setExpiryTimer,
              (absl::string_view resource_name, std::chrono::milliseconds ms));
  MOCK_METHOD(void, disableExpiryTimer, (absl::string_view resource_name));
};

} // namespace Config
} // namespace Envoy
