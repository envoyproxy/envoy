#pragma once

#include "envoy/config/xds_manager.h"

#include "source/common/config/xds_symbol_table.h"

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
  MOCK_METHOD(void, startXdstpAdsMuxes, ());
  MOCK_METHOD(ScopedResume, pause, (const std::string& type_url), (override));
  MOCK_METHOD(ScopedResume, pause, (const std::vector<std::string>& type_urls), (override));
  MOCK_METHOD(absl::StatusOr<SubscriptionPtr>, subscribeToSingletonResource,
              (absl::string_view resource_name,
               OptRef<const envoy::config::core::v3::ConfigSource> config,
               absl::string_view type_url, Stats::Scope& scope, SubscriptionCallbacks& callbacks,
               OpaqueResourceDecoderSharedPtr resource_decoder,
               const SubscriptionOptions& options));
  MOCK_METHOD(
      absl::StatusOr<std::unique_ptr<SingletonSubscription>>, subscribeToSingletonResource,
      (absl::string_view resource_name, OptRef<const envoy::config::core::v3::ConfigSource> config,
       absl::string_view type_url, Stats::Scope& scope, SingletonSubscriptionCallbacks& callbacks,
       OpaqueResourceDecoderSharedPtr resource_decoder, const SubscriptionOptions& options));
  MOCK_METHOD(void, shutdown, ());
  MOCK_METHOD(absl::Status, setAdsConfigSource,
              (const envoy::config::core::v3::ApiConfigSource& config_source));
  MOCK_METHOD(absl::Status, initializeAdsConnections,
              (const envoy::config::bootstrap::v3::Bootstrap& bootstrap));
  MOCK_METHOD(GrpcMuxSharedPtr, adsMux, ());
  MOCK_METHOD(SubscriptionFactory&, subscriptionFactory, ());
  MOCK_METHOD(XdsSymbolTable&, xdsSymbolTable, (), (override));

  testing::NiceMock<MockSubscriptionFactory> subscription_factory_;
  std::shared_ptr<testing::NiceMock<MockGrpcMux>> ads_mux_;
  XdsSymbolTableSharedPtr xds_symbol_table_{XdsSymbolTable::create()};
};

} // namespace Config
} // namespace Envoy
