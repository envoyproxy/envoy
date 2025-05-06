#pragma once

#include "envoy/config/xds_manager.h"

#include "test/mocks/config/mocks.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockXdsManager : public XdsManager {
public:
  MockXdsManager();
  ~MockXdsManager() override = default;

  MOCK_METHOD(absl::Status, initialize,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
               Upstream::ClusterManager* cm));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(absl::Status, setAdsConfigSource,
              (const envoy::config::core::v3::ApiConfigSource& config_source));
  MOCK_METHOD(absl::Status, initializeAdsConnections,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(GrpcMuxSharedPtr, adsMux, ());
  MOCK_METHOD(SubscriptionFactory&, subscriptionFactory, ());

  testing::NiceMock<MockSubscriptionFactory> subscription_factory_;
  std::shared_ptr<testing::NiceMock<MockGrpcMux>> ads_mux_;
};

} // namespace Config
} // namespace Envoy
