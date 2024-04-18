#pragma once

#include "envoy/upstream/retry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockRetryPriority : public RetryPriority {
public:
  MockRetryPriority(const HealthyLoad& healthy_priority_load,
                    const DegradedLoad& degraded_priority_load)
      : priority_load_({healthy_priority_load, degraded_priority_load}) {}
  MockRetryPriority(const MockRetryPriority& other) : priority_load_(other.priority_load_) {}
  ~MockRetryPriority() override;

  const HealthyAndDegradedLoad& determinePriorityLoad(const PrioritySet&,
                                                      const HealthyAndDegradedLoad&,
                                                      const PriorityMappingFunc&) override {
    return priority_load_;
  }

  MOCK_METHOD(void, onHostAttempted, (HostDescriptionConstSharedPtr));

private:
  const HealthyAndDegradedLoad priority_load_;
};
} // namespace Upstream
} // namespace Envoy
