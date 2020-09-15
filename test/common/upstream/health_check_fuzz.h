#pragma once
#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HealthCheckFuzz { // TODO: once added tcp/grpc, switch this to an
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
  void initializeAndReplayHttp(test::common::upstream::HealthCheckTestCase input);
  void initializeAndReplayTcp(test::common::upstream::HealthCheckTestCase input);
  void respondHttp(const test::fuzz::Headers& headers, absl::string_view status);
  void respondTcp();
  void triggerIntervalTimerHttp();
  void triggerIntervalTimerTcp();
  void triggerTimeoutTimerHttp(bool last_action);
  void triggerTimeoutTimerTcp(bool last_action);
  void allocHttpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void allocTcpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void raiseEvent(const test::common::upstream::RaiseEvent& event, bool last_action);

  void replay(const test::common::upstream::HealthCheckTestCase& input);

  // Determines whether the client gets reused or not after respondHeaders()
  bool reuse_connection_ = true;

  HttpHealthCheckerImplTestBase http_test_base_;
  TcpHealthCheckerImplTestBase tcp_test_base_;
};

} // namespace Upstream
} // namespace Envoy
