#pragma once

#include "envoy/config/config_provider_manager.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/typed_config.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/config/config_provider_impl.h"
#include "common/protobuf/utility.h"

#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

class MockSubscriptionCallbacks : public SubscriptionCallbacks {
public:
  MockSubscriptionCallbacks();
  ~MockSubscriptionCallbacks() override;

  MOCK_METHOD(void, onConfigUpdate,
              (const std::vector<DecodedResourceRef>& resources, const std::string& version_info));
  MOCK_METHOD(void, onConfigUpdate,
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
  MOCK_METHOD(
      void, onConfigUpdate,
      (const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
       const Protobuf::RepeatedPtrField<std::string>& removed_resources,
       const std::string& system_version_info));
  MOCK_METHOD(void, onConfigUpdateFailed,
              (Envoy::Config::ConfigUpdateFailureReason reason, const EnvoyException* e));
};

class MockSubscription : public Subscription {
public:
  MOCK_METHOD(void, start, (const std::set<std::string>& resources));
  MOCK_METHOD(void, updateResourceInterest, (const std::set<std::string>& update_to_these_names));
};

class MockSubscriptionFactory : public SubscriptionFactory {
public:
  MockSubscriptionFactory();
  ~MockSubscriptionFactory() override;

  MOCK_METHOD(SubscriptionPtr, subscriptionFromConfigSource,
              (const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
               Stats::Scope& scope, SubscriptionCallbacks& callbacks,
               OpaqueResourceDecoder& resource_decoder));
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
              (const std::set<std::string>& resources, const std::string& type_url,
               SubscriptionCallbacks& callbacks, SubscriptionStats& stats,
               std::chrono::milliseconds init_fetch_timeout));
  MOCK_METHOD(void, updateResourceInterest,
              (const std::set<std::string>& resources, const std::string& type_url));

  MOCK_METHOD(GrpcMuxWatchPtr, addWatch,
              (const std::string& type_url, const std::set<std::string>& resources,
               SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder));
};

class MockGrpcStreamCallbacks
    : public GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  MockGrpcStreamCallbacks();
  ~MockGrpcStreamCallbacks() override;

  MOCK_METHOD(void, onStreamEstablished, ());
  MOCK_METHOD(void, onEstablishmentFailure, ());
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
              (const Protobuf::Message& config_proto,
               Server::Configuration::ServerFactoryContext& factory_context,
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

} // namespace Config
} // namespace Envoy
