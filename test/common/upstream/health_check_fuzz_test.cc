#include "envoy/config/core/v3/health_check.pb.validate.h"

#include "test/common/upstream/health_check_fuzz.h"
#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Upstream {

DEFINE_PROTO_FUZZER(const test::common::upstream::HealthCheckTestCase input) {
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  std::unique_ptr<HealthCheckFuzz> health_check_fuzz;

  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: {
    health_check_fuzz = std::make_unique<HttpHealthCheckFuzz>();
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    health_check_fuzz = std::make_unique<TcpHealthCheckFuzz>();
    break;
  }
  case envoy::config::core::v3::HealthCheck::kGrpcHealthCheck: {
    health_check_fuzz = std::make_unique<GrpcHealthCheckFuzz>();
    break;
  }
  default: // Handles custom health checker
    ENVOY_LOG_MISC(trace, "Custom Health Checker currently unsupported, skipping");
    return;
  }

  health_check_fuzz->initializeAndReplay(input);
}

} // namespace Upstream
} // namespace Envoy
