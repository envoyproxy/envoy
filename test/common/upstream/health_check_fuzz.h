#pragma once
#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HealthCheckFuzz : public HttpHealthCheckerImplTestBase {
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
  void respondHttp(test::fuzz::Headers headers, absl::string_view status);
  void triggerIntervalTimer();
  void triggerTimeoutTimer(bool last_action);
  void allocHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void raiseEvent(test::common::upstream::RaiseEvent event, bool last_action);

  void replay(test::common::upstream::HealthCheckTestCase input);

  bool reuse_connection_;
};

} // namespace Upstream
} // namespace Envoy
