#pragma once

#include "envoy/config/ads.h"
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
};

class MockAdsWatch : public AdsWatch {
public:
  MOCK_METHOD0(cancel, void());
};

class MockAdsApi : public AdsApi {
public:
  MOCK_METHOD3(subscribe,
               AdsWatch*(const std::string& type_url, const std::vector<std::string>& resources,
                         AdsCallbacks& calllbacks));
};

} // namespace Config
} // namespace Envoy
