#pragma once

#include "envoy/config/subscription.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Config {

template <class ResourceType>
class MockSubscriptionCallbacks : public SubscriptionCallbacks<ResourceType> {
public:
  MOCK_METHOD1_T(
      onConfigUpdate,
      bool(const typename SubscriptionCallbacks<ResourceType>::ResourceVector& resources));
};

template <class ResourceType> class MockSubscription : public Subscription<ResourceType> {
public:
  MOCK_METHOD2_T(start, void(const std::vector<std::string>& resources,
                             SubscriptionCallbacks<ResourceType>& callbacks));
  MOCK_METHOD1_T(updateResources, void(const std::vector<std::string>& resources));
};

} // Config
} // Envoy
