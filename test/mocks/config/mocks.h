#pragma once

#include "envoy/api/v2/eds.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/config/xds_grpc_context.h"

#include "common/config/config_provider_impl.h"
#include "common/config/resources.h"
#include "common/protobuf/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class ResourceType> class MockSubscriptionCallbacks : public SubscriptionCallbacks {
public:
  MockSubscriptionCallbacks() {
    ON_CALL(*this, resourceName(testing::_))
        .WillByDefault(testing::Invoke([](const ProtobufWkt::Any& resource) -> std::string {
          return resourceName_(MessageUtil::anyConvert<ResourceType>(resource));
        }));
  }
  ~MockSubscriptionCallbacks() override {}
  static std::string resourceName_(const envoy::api::v2::ClusterLoadAssignment& resource) {
    return resource.cluster_name();
  }
  template <class T> static std::string resourceName_(const T& resource) { return resource.name(); }

  // TODO(fredlas) deduplicate
  MOCK_METHOD2_T(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                      const std::string& version_info));
  MOCK_METHOD3_T(onConfigUpdate,
                 void(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info));
  MOCK_METHOD1_T(onConfigUpdateFailed, void(const EnvoyException* e));
  MOCK_METHOD1_T(resourceName, std::string(const ProtobufWkt::Any& resource));
};

class MockSubscription : public Subscription {
public:
  MOCK_METHOD2_T(start,
                 void(const std::set<std::string>& resources, SubscriptionCallbacks& callbacks));
  MOCK_METHOD1_T(updateResources, void(const std::set<std::string>& update_to_these_names));
};

class MockGrpcMuxWatch : public GrpcMuxWatch {
public:
  MockGrpcMuxWatch();
  ~MockGrpcMuxWatch();

  MOCK_METHOD0(cancel, void());
};

class MockGrpcMux : public GrpcMux {
public:
  MockGrpcMux();
  ~MockGrpcMux();

  MOCK_METHOD0(start, void());
  MOCK_METHOD3(subscribe_,
               GrpcMuxWatch*(const std::string& type_url, const std::set<std::string>& resources,
                             GrpcMuxCallbacks& callbacks));
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::set<std::string>& resources,
                            GrpcMuxCallbacks& callbacks);
  MOCK_METHOD1(pause, void(const std::string& type_url));
  MOCK_METHOD1(resume, void(const std::string& type_url));
};

class MockGrpcMuxCallbacks : public GrpcMuxCallbacks {
public:
  MockGrpcMuxCallbacks();
  ~MockGrpcMuxCallbacks();

  MOCK_METHOD2(onConfigUpdate, void(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                    const std::string& version_info));
  MOCK_METHOD1(onConfigUpdateFailed, void(const EnvoyException* e));
  MOCK_METHOD1(resourceName, std::string(const ProtobufWkt::Any& resource));
};

class MockGrpcStreamCallbacks : public GrpcStreamCallbacks<envoy::api::v2::DiscoveryResponse> {
public:
  MockGrpcStreamCallbacks();
  ~MockGrpcStreamCallbacks();

  MOCK_METHOD0(onStreamEstablished, void());
  MOCK_METHOD0(onEstablishmentFailure, void());
  MOCK_METHOD1(onDiscoveryResponse,
               void(std::unique_ptr<envoy::api::v2::DiscoveryResponse>&& message));
  MOCK_METHOD0(onWriteable, void());
};

class MockMutableConfigProviderBase : public MutableConfigProviderBase {
public:
  MockMutableConfigProviderBase(std::shared_ptr<ConfigSubscriptionInstance>&& subscription,
                                ConfigProvider::ConfigConstSharedPtr initial_config,
                                Server::Configuration::FactoryContext& factory_context);

  MOCK_CONST_METHOD0(getConfig, ConfigConstSharedPtr());
  MOCK_METHOD1(onConfigProtoUpdate, ConfigConstSharedPtr(const Protobuf::Message& config_proto));
  MOCK_METHOD1(initialize, void(const ConfigConstSharedPtr& initial_config));
  MOCK_METHOD1(onConfigUpdate, void(const ConfigConstSharedPtr& config));

  ConfigSubscriptionCommonBase& subscription() { return *subscription_.get(); }
};

} // namespace Config
} // namespace Envoy
