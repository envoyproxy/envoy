#pragma once

#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/config_provider.h"
#include "envoy/router/rds.h"

#include "test/mocks/router/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {
namespace ConnectionManagerImplHelper {

// Test RouteConfigProvider that returns a mocked config.
struct RouteConfigProvider : public Router::RouteConfigProvider {
  RouteConfigProvider(TimeSource& time_source) : time_source_(time_source) {}

  // Router::RouteConfigProvider
  Router::ConfigConstSharedPtr config() override { return route_config_; }
  absl::optional<ConfigInfo> configInfo() const override { return {}; }
  SystemTime lastUpdated() const override { return time_source_.systemTime(); }
  void onConfigUpdate() override {}
  void validateConfig(const envoy::api::v2::RouteConfiguration&) const override {}

  TimeSource& time_source_;
  std::shared_ptr<Router::MockConfig> route_config_{new NiceMock<Router::MockConfig>()};
};

// Test ScopedRouteConfigProvider that returns a mocked config.
struct ScopedRouteConfigProvider : public Config::ConfigProvider {
  ScopedRouteConfigProvider(TimeSource& time_source)
      : config_(std::make_shared<Router::MockScopedConfig>()), time_source_(time_source) {}

  ~ScopedRouteConfigProvider() override = default;

  // Config::ConfigProvider
  SystemTime lastUpdated() const override { return time_source_.systemTime(); }
  const Protobuf::Message* getConfigProto() const override { return nullptr; }
  Envoy::Config::ConfigProvider::ConfigProtoVector getConfigProtos() const override { return {}; }
  std::string getConfigVersion() const override { return ""; }
  ConfigConstSharedPtr getConfig() const override { return config_; }
  ApiType apiType() const override { return ApiType::Delta; }

  std::shared_ptr<Router::MockScopedConfig> config_;
  TimeSource& time_source_;
};

} // namespace ConnectionManagerImplHelper
} // namespace Http
} // namespace Envoy
