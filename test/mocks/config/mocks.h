#pragma once

#include "envoy/config/config_provider_manager.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/callback_impl.h"
#include "source/common/config/config_provider_impl.h"
#include "source/common/protobuf/utility.h"

#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockSubscriptionCallbacks : public SubscriptionCallbacks {
public:
  MockSubscriptionCallbacks();
  ~MockSubscriptionCallbacks() override;

  MOCK_METHOD(absl::Status, onConfigUpdate,
              (const std::vector<DecodedResourceRef>& resources, const std::string& version_info));
  MOCK_METHOD(absl::Status, onConfigUpdate,
              (const std::vector<DecodedResourceRef>& added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources,
               const std::string& system_version_info));
  MOCK_METHOD(void, onConfigUpdateFailed,
              (Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException* e));
};

class MockOpaqueResourceDecoder : public OpaqueResourceDecoder {
public:
  MockOpaqueResourceDecoder();
  ~MockOpaqueResourceDecoder() override;

  MOCK_METHOD(ProtobufTypes::MessagePtr, decodeResource, (const ProtobufWkt::Any& resource));
  MOCK_METHOD(std::string, resourceName, (const Protobuf::Message& resource));
};

class MockUntypedConfigUpdateCallbacks : public UntypedConfigUpdateCallbacks {
public:
  MockUntypedConfigUpdateCallbacks();
  ~MockUntypedConfigUpdateCallbacks() override;

  MOCK_METHOD(void, onConfigUpdate,
              (const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
               const std::string& version_info));

  MOCK_METHOD(void, onConfigUpdate,
              (const std::vector<DecodedResourcePtr>& resources, const std::string& version_info));

  MOCK_METHOD(void, onConfigUpdate,
              (absl::Span<const envoy::service::discovery::v3::Resource* const> added_resources,
               const Protobuf::RepeatedPtrField<std::string>& removed_resources,
               const std::string& system_version_info));
  MOCK_METHOD(void, onConfigUpdateFailed,
              (Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException* e));
};

class MockSubscription : public Subscription {
public:
  MOCK_METHOD(void, start, (const absl::flat_hash_set<std::string>& resources));
  MOCK_METHOD(void, updateResourceInterest,
              (const absl::flat_hash_set<std::string>& update_to_these_names));
  MOCK_METHOD(void, requestOnDemandUpdate,
              (const absl::flat_hash_set<std::string>& add_these_names));
};

class MockSubscriptionFactory : public SubscriptionFactory {
public:
  MockSubscriptionFactory();
  ~MockSubscriptionFactory() override;

  MOCK_METHOD(absl::StatusOr<SubscriptionPtr>, subscriptionFromConfigSource,
              (const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
               Stats::Scope& scope, SubscriptionCallbacks& callbacks,
               OpaqueResourceDecoderSharedPtr resource_decoder,
               const SubscriptionOptions& options));
  MOCK_METHOD(absl::StatusOr<SubscriptionPtr>, collectionSubscriptionFromUrl,
              (const xds::core::v3::ResourceLocator& collection_locator,
               const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
               Stats::Scope& scope, SubscriptionCallbacks& callbacks,
               OpaqueResourceDecoderSharedPtr resource_decoder));
  MOCK_METHOD(ProtobufMessage::ValidationVisitor&, messageValidationVisitor, ());

  MockSubscription* subscription_{};
  SubscriptionCallbacks* callbacks_{};
};

class MockGrpcMuxWatch : public GrpcMuxWatch {
public:
  MockGrpcMuxWatch();
  ~MockGrpcMuxWatch() override;

  MOCK_METHOD(void, cancel, ());
};

class MockGrpcMux : public GrpcMux {
public:
  MockGrpcMux();
  ~MockGrpcMux() override;

  MOCK_METHOD(void, start, (), (override));
  MOCK_METHOD(ScopedResume, pause, (const std::string& type_url), (override));
  MOCK_METHOD(ScopedResume, pause, (const std::vector<std::string> type_urls), (override));

  MOCK_METHOD(void, addSubscription,
              (const absl::flat_hash_set<std::string>& resources, const std::string& type_url,
               SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
               std::chrono::milliseconds init_fetch_timeout));
  MOCK_METHOD(void, updateResourceInterest,
              (const absl::flat_hash_set<std::string>& resources, const std::string& type_url));

  MOCK_METHOD(GrpcMuxWatchPtr, addWatch,
              (const std::string& type_url, const absl::flat_hash_set<std::string>& resources,
               SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
               const SubscriptionOptions& options));

  MOCK_METHOD(void, requestOnDemandUpdate,
              (const std::string& type_url,
               const absl::flat_hash_set<std::string>& add_these_names));

  MOCK_METHOD(bool, paused, (const std::string& type_url), (const));

  MOCK_METHOD(EdsResourcesCacheOptRef, edsResourcesCache, ());

  MOCK_METHOD(absl::Status, updateMuxSource,
              (Grpc::RawAsyncClientPtr && primary_async_client,
               Grpc::RawAsyncClientPtr&& failover_async_client,
               CustomConfigValidatorsPtr&& custom_config_validators, Stats::Scope& scope,
               BackOffStrategyPtr&& backoff_strategy,
               const envoy::config::core::v3::ApiConfigSource& ads_config_source));
};

class MockGrpcStreamCallbacks
    : public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  MockGrpcStreamCallbacks();
  ~MockGrpcStreamCallbacks() override;

  MOCK_METHOD(void, onStreamEstablished, ());
  MOCK_METHOD(void, onEstablishmentFailure, (bool));
  MOCK_METHOD(void, onDiscoveryResponse,
              (std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse> && message,
               ControlPlaneStats& control_plane_stats));
  MOCK_METHOD(void, onWriteable, ());
};

class MockConfigProviderManager : public ConfigProviderManager {
public:
  MockConfigProviderManager() = default;
  ~MockConfigProviderManager() override = default;

  MOCK_METHOD(ConfigProviderPtr, createXdsConfigProvider,
              (const Protobuf::Message& config_source_proto,
               Server::Configuration::ServerFactoryContext& factory_context,
               Init::Manager& init_manager, const std::string& stat_prefix,
               const Envoy::Config::ConfigProviderManager::OptionalArg& optarg));
  MOCK_METHOD(ConfigProviderPtr, createStaticConfigProvider,
              (std::vector<std::unique_ptr<const Protobuf::Message>> && config_protos,
               Server::Configuration::ServerFactoryContext& factory_context,
               const Envoy::Config::ConfigProviderManager::OptionalArg& optarg));
};

class MockTypedFactory : public TypedFactory {
public:
  ~MockTypedFactory() override;

  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, ());
  MOCK_METHOD(std::string, configType, ());
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(std::string, category, (), (const));
};

class MockContextProvider : public ContextProvider {
public:
  MockContextProvider();
  ~MockContextProvider() override;

  MOCK_METHOD(const xds::core::v3::ContextParams&, nodeContext, (), (const));
  MOCK_METHOD(const xds::core::v3::ContextParams&, dynamicContext,
              (absl::string_view resource_type_url), (const));
  MOCK_METHOD(void, setDynamicContextParam,
              (absl::string_view resource_type_url, absl::string_view key,
               absl::string_view value));
  MOCK_METHOD(void, unsetDynamicContextParam,
              (absl::string_view resource_type_url, absl::string_view key));
  MOCK_METHOD(Common::CallbackHandlePtr, addDynamicContextUpdateCallback,
              (UpdateNotificationCb callback), (const));

  Common::CallbackManager<absl::string_view> update_cb_handler_;
};

template <class FactoryCallback>
class TestExtensionConfigProvider : public Config::ExtensionConfigProvider<FactoryCallback> {
public:
  TestExtensionConfigProvider(FactoryCallback cb) : cb_(cb) {}
  const std::string& name() override { return name_; }
  OptRef<FactoryCallback> config() override { return {cb_}; }

private:
  const std::string name_ = "mock_config_provider";
  FactoryCallback cb_;
};

} // namespace Config
} // namespace Envoy
