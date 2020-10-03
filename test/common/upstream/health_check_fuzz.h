#pragma once

#include <memory>

#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

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

  //The specific implementations of this look into this proto to their type to get the data
  virtual respond(test::common::upstream::Respond respond) PURE;

  virtual void initialize(test::common::upstream::HealthCheckTestCase input) PURE;
  virtual void triggerIntervalTimer() PURE; //TODO: Tcp doesn't use expect client create?
  virtual void triggerTimeoutTimer(bool last_action) PURE;
  virtual void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) PURE;

private:
  Network::ConnectionEvent getEventTypeFromProto(const test::common::upstream::RaiseEvent& event);

  void replay(const test::common::upstream::HealthCheckTestCase& input);

  Type type_;
  std::unique_ptr<HttpHealthCheckFuzz> http_fuzz_test_;
  std::unique_ptr<TcpHealthCheckFuzz> tcp_fuzz_test_;
  std::unique_ptr<GrpcHealthCheckFuzz> grpc_fuzz_test_;
};

//Have a single base class with all of the methods shared across the three health check fuzz tests as virtuals, then implement those specifically

class HttpHealthCheckFuzz : HealthCheckFuzz, HttpHealthCheckerImplTestBase {
public:
  void allocHttpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  void respondHttp(const test::fuzz::Headers& headers, uint64_t status);
  void respond(test::common::upstream::Respond respond) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action) override;
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) override;

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;
};

class TcpHealthCheckFuzz : HealthCheckFuzz, TcpHealthCheckerImplTestBase {
public:
  void allocTcpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  void respondTcp(std::string data, bool last_action);
  void respond(test::common::upstream::Respond respond) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action);
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action);

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Empty response induces a specific codepath in raiseEvent in case of connected, ignores the
  // binary field and only uses text.
  bool empty_response_ = true;
};

class GrpcHealthCheckFuzz : HealthCheckFuzz, GrpcHealthCheckerImplTestBaseUtils {
public:
  void allocGrpcHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  // This has three components, headers, raw bytes, and trailers
  void respondGrpc(test::common::upstream::GrpcRespond grpc_respond);
  void respond(test::common::upstream::Respond respond) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action) override;
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) override;
  void raiseGoAway(bool no_error);

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Determines whether a client closes after responds and timeouts. Exactly maps to
  // received_no_error_goaway_ in source code.
  bool received_no_error_goaway_ = false;
};

} // namespace Upstream
} // namespace Envoy
