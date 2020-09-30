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

  HealthCheckFuzz health_check_fuzz;

  if (input.health_check_config().health_checker_case() ==
      envoy::config::core::v3::HealthCheck::kGrpcHealthCheck) {
    ENVOY_LOG_MISC(trace, "Fuzz engine created GRPC Health Checker");
    return;
  }

  health_check_fuzz.initializeAndReplay(input);
}

} // namespace Upstream
} // namespace Envoy
