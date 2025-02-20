#pragma once

#include "envoy/config/xds_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockXdsManager : public XdsManager {
public:
  MockXdsManager();
  ~MockXdsManager() override = default;

  MOCK_METHOD(absl::Status, initialize, (Upstream::ClusterManager * cm));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(absl::Status, setAdsConfigSource,
              (const envoy::config::core::v3::ApiConfigSource& config_source));
};

} // namespace Config
} // namespace Envoy
