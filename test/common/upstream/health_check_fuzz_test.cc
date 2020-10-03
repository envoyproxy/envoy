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

  switch (input.health_check_config().health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::kHttpHealthCheck: {
    std::unique_ptr<HttpHealthCheckFuzz> http_health_check_fuzz =
        std::make_unique<HttpHealthCheckFuzz>();
    http_health_check_fuzz->initializeAndReplay(input);
    break;
  }
  case envoy::config::core::v3::HealthCheck::kTcpHealthCheck: {
    std::unique_ptr<TcpHealthCheckFuzz> tcp_health_check_fuzz =
        std::make_unique<TcpHealthCheckFuzz>();
    tcp_health_check_fuzz->initializeAndReplay(input);
    break;
  }
  case envoy::config::core::v3::HealthCheck::kGrpcHealthCheck: {
    std::unique_ptr<GrpcHealthCheckFuzz> grpc_health_check_fuzz =
        std::make_unique<GrpcHealthCheckFuzz>();
    grpc_health_check_fuzz->initializeAndReplay(input);
    break;
  }
  default:
    break;
  }
}

} // namespace Upstream
} // namespace Envoy
