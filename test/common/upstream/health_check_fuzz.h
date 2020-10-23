#pragma once

#include <memory>

#include "test/common/upstream/health_check_fuzz.pb.validate.h"
#include "test/common/upstream/health_checker_impl_test_utils.h"
#include "test/fuzz/common.pb.h"

namespace Envoy {
namespace Upstream {

class HealthCheckFuzzBase {
public:
  std::shared_ptr<MockClusterMockPrioritySet> cluster_{
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<MockHealthCheckEventLogger> event_logger_storage_{
      std::make_unique<MockHealthCheckEventLogger>()};
  MockHealthCheckEventLogger& event_logger_{*event_logger_storage_};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
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

  // The specific implementations of respond look into the respond proto, which has all three types
  // of response
  virtual void respond(test::common::upstream::Respond respond, bool last_action) PURE;

  virtual void initialize(test::common::upstream::HealthCheckTestCase input) PURE;
  virtual void triggerIntervalTimer(bool expect_client_create) PURE;
  virtual void triggerTimeoutTimer(bool last_action) PURE;
  virtual void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) PURE;

  virtual ~HealthCheckFuzz() = default;

private:
  Network::ConnectionEvent getEventTypeFromProto(const test::common::upstream::RaiseEvent& event);

  void replay(const test::common::upstream::HealthCheckTestCase& input);
};

class HttpHealthCheckFuzz : public HealthCheckFuzz, HttpHealthCheckerImplTestBase {
public:
  struct TestSession {
    NiceMock<Event::MockTimer>* interval_timer_{};
    NiceMock<Event::MockTimer>* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockRequestEncoder> request_encoder_;
    Http::ResponseDecoder* stream_response_callbacks_{};
  };

  using TestSessionPtr = std::unique_ptr<TestSession>;
  using HostWithHealthCheckMap =
      absl::node_hash_map<std::string,
                          const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig>;

  void allocHttpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  void respond(test::common::upstream::Respond respond, bool last_action) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action) override;
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) override;
  ~HttpHealthCheckFuzz() override = default;

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // State
  TestSessionPtr test_session_;
  std::shared_ptr<TestHttpHealthCheckerImpl> health_checker_;
  std::list<uint32_t> connection_index_{};
  std::list<uint32_t> codec_index_{};
  const HostWithHealthCheckMap health_checker_map_{};

private:
  void expectSessionCreate(const HostWithHealthCheckMap& health_check_map);

  void expectClientCreate(size_t index, const HostWithHealthCheckMap& health_check_map);

  void expectStreamCreate(size_t index);

  void expectSessionCreate();
  void expectClientCreate(size_t index);
};

class TcpHealthCheckFuzz : public HealthCheckFuzz, TcpHealthCheckerImplTestBase {
public:
  void allocTcpHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  void respond(test::common::upstream::Respond respond, bool last_action) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action) override;
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) override;
  ~TcpHealthCheckFuzz() override = default;

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Empty response induces a specific codepath in raiseEvent in case of connected, ignores the
  // binary field and only uses text.
  bool empty_response_ = true;
};

class GrpcHealthCheckFuzz : public HealthCheckFuzz, GrpcHealthCheckerImplTestBaseUtils {
public:
  void allocGrpcHealthCheckerFromProto(const envoy::config::core::v3::HealthCheck& config);
  void initialize(test::common::upstream::HealthCheckTestCase input) override;
  // This has three components, headers, raw bytes, and trailers
  void respond(test::common::upstream::Respond respond, bool last_action) override;
  void triggerIntervalTimer(bool expect_client_create) override;
  void triggerTimeoutTimer(bool last_action) override;
  void raiseEvent(const Network::ConnectionEvent& event_type, bool last_action) override;
  void raiseGoAway(bool no_error);
  ~GrpcHealthCheckFuzz() override = default;

  // Determines whether the client gets reused or not after response
  bool reuse_connection_ = true;

  // Determines whether a client closes after responds and timeouts. Exactly maps to
  // received_no_error_goaway_ in source code.
  bool received_no_error_goaway_ = false;
};

} // namespace Upstream
} // namespace Envoy
