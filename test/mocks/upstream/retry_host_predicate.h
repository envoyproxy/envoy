#pragma once

#include "envoy/upstream/retry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockRetryHostPredicate : public RetryHostPredicate {
public:
  MockRetryHostPredicate();
  ~MockRetryHostPredicate() override;

  MOCK_METHOD(bool, shouldSelectAnotherHost, (const Host& candidate_host), (override));
  MOCK_METHOD(void, onHostAttempted, (HostDescriptionConstSharedPtr), (override));
};
} // namespace Upstream
} // namespace Envoy
