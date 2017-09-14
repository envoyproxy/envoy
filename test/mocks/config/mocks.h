#pragma once

#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class ResourceType>
class MockSubscriptionCallbacks : public SubscriptionCallbacks<ResourceType> {
public:
  MOCK_METHOD1_T(
      onConfigUpdate,
      void(const typename SubscriptionCallbacks<ResourceType>::ResourceVector& resources));
  MOCK_METHOD1_T(onConfigUpdateFailed, void(const EnvoyException* e));
};

template <class ResourceType> class MockSubscription : public Subscription<ResourceType> {
public:
  MOCK_METHOD2_T(start, void(const std::vector<std::string>& resources,
                             SubscriptionCallbacks<ResourceType>& callbacks));
  MOCK_METHOD1_T(updateResources, void(const std::vector<std::string>& resources));

  MOCK_CONST_METHOD0_T(versionInfo, const std::string());
};

class MockGrpcMuxWatch : public GrpcMuxWatch {
public:
  MockGrpcMuxWatch();
  virtual ~MockGrpcMuxWatch();

  MOCK_METHOD0(cancel, void());
};

class MockGrpcMux : public GrpcMux {
public:
  MockGrpcMux();
  virtual ~MockGrpcMux();

  MOCK_METHOD0(start, void());
  MOCK_METHOD3(subscribe_,
               GrpcMuxWatch*(const std::string& type_url, const std::vector<std::string>& resources,
                             GrpcMuxCallbacks& callbacks));
  GrpcMuxWatchPtr subscribe(const std::string& type_url, const std::vector<std::string>& resources,
                            GrpcMuxCallbacks& callbacks);
};

} // namespace Config
} // namespace Envoy
