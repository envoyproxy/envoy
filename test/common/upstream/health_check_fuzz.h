#pragma once

#include <memory>

#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HttpHealthCheckFuzz : HttpHealthCheckerImplTestBase {
public:
  void allocHttpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input);
  void respond(const test::fuzz::Headers& headers, uint64_t status);
  void triggerIntervalTimer(bool expect_client_create);
  void triggerTimeoutTimer(bool last_action);
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action);

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;
};

class TcpHealthCheckFuzz : TcpHealthCheckerImplTestBase {
public:
  void allocTcpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input);
  void respond(std::string data, bool last_action);
  void triggerIntervalTimer();
  void triggerTimeoutTimer(bool last_action);
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action);

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Empty response induces a specific codepath in raiseEvent in case of connected, ignores the
  // binary field and only uses text.
  bool empty_response_ = true;
};

class GrpcHealthCheckFuzz : GrpcHealthCheckerImplTestBaseUtils {
public:
  void allocGrpcHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input);
  // This has three components, headers, raw bytes, and trailers
  void respond(test::common::upstream::GrpcRespond grpc_respond);
  void triggerIntervalTimer(bool expect_client_create);
  void triggerTimeoutTimer(bool last_action);
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action);
  void raiseGoAway(bool no_error);

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Determines whether a client closes after responds and timeouts. Exactly maps to
  // received_no_error_goaway_ in source code.
  bool received_no_error_goaway_ = false;
};

class HealthCheckFuzz {
public:
  HealthCheckFuzz() = default;
  // This will delegate to the specific classes
  void initializeAndReplay(test::common::upstream::HealthCheckTestCase input);
  enum class Type {
    HTTP,
    TCP,
    GRPC,
  };

private:
  Network::ConnectionEvent getEventTypeFromProto(const test::common::upstream::RaiseEvent& event);

  void replay(const test::common::upstream::HealthCheckTestCase& input);

  Type type_;
  std::unique_ptr<HttpHealthCheckFuzz> http_fuzz_test_;
  std::unique_ptr<TcpHealthCheckFuzz> tcp_fuzz_test_;
  std::unique_ptr<GrpcHealthCheckFuzz> grpc_fuzz_test_;
};

} // namespace Upstream
} // namespace Envoy
