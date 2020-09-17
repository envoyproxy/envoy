#pragma once
#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HttpHealthCheckFuzz : HttpHealthCheckerImplTestBase {
public:
  void allocHttpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input);
  void respond(const test::fuzz::Headers& headers, absl::string_view status);
  void triggerIntervalTimer(bool last_action);
  void triggerTimeoutTimer(bool last_action);
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action);

  // Determines whether the client gets reused or not after respondHeaders()
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

  // Determines whether the client gets reused or not after respondHeaders()
  bool reuse_connection_ = true;
};

class HealthCheckFuzz { // TODO: once added tcp/grpc, switch this to an
                        // abstract health checker test class that can handle
                        // one of the three types
public:
  HealthCheckFuzz() = default;
  void initializeAndReplay(test::common::upstream::HealthCheckTestCase
                               input); // This will delegate to the specific classes
  enum class Type {
    HTTP,
    TCP,
    GRPC,
  };

private:
  void raiseEvent(const test::common::upstream::RaiseEvent& event, bool last_action);

  void replay(const test::common::upstream::HealthCheckTestCase& input);

  Type type_;
  HttpHealthCheckFuzz* http_fuzz_test_;
  TcpHealthCheckFuzz* tcp_fuzz_test_;
};

} // namespace Upstream
} // namespace Envoy
