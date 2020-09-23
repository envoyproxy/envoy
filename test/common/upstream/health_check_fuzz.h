#pragma once

#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HealthCheckFuzz
    : public HttpHealthCheckerImplTestBase { // TODO: once added tcp/grpc, switch this to an
                                             // abstract health checker test class that can handle
                                             // one of the three types
public:
  HealthCheckFuzz() = default;
  void initializeAndReplay(test::common::upstream::HealthCheckTestCase input);
  enum class Type {
    HTTP,
    TCP,
    GRPC,
  };

  Type type_;

private:
  void respondHttp(const test::fuzz::Headers& headers, uint64_t status);
  void triggerIntervalTimer(bool expect_client_create);
  void triggerTimeoutTimer(bool last_action);
  void allocHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void raiseEvent(const test::common::upstream::RaiseEvent& event, bool last_action);

  void replay(const test::common::upstream::HealthCheckTestCase& input);

  // Determines whether the client gets reused or not after respondHeaders()
  bool reuse_connection_ = true;
};

} // namespace Upstream
} // namespace Envoy
