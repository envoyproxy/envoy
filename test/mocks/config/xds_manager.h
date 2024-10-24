#pragma once

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockXdsManager : public XdsManager {
public:
  MockXdsManager() = default;
  ~MockXdsManager() override = default;

  MOCK_METHOD(absl::Status, setAdsConfigSource,
              (const envoy::config::core::v3::ApiConfigSource& config_source));
};

} // namespace Config
} // namespace Envoy
