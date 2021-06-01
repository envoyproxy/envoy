#pragma once

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/upstream/health_checker.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
class MockHealthCheckEventLogger : public HealthCheckEventLogger {
public:
  MOCK_METHOD(void, logEjectUnhealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               envoy::data::core::v3::HealthCheckFailureType));
  MOCK_METHOD(void, logAddHealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               bool));
  MOCK_METHOD(void, logUnhealthy,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&,
               envoy::data::core::v3::HealthCheckFailureType, bool));
  MOCK_METHOD(void, logDegraded,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&));
  MOCK_METHOD(void, logNoLongerDegraded,
              (envoy::data::core::v3::HealthCheckerType, const HostDescriptionConstSharedPtr&));
};
} // namespace Upstream

} // namespace Envoy
