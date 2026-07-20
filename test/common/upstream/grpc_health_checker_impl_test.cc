#include <chrono>
#include <memory>
#include <ostream>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/core/v3/health_check.pb.validate.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/health_check/event_sinks/file/v3/file.pb.h"
#include "envoy/upstream/health_check_host_monitor.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/base64.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/health_checker_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/health_checkers/grpc/health_checker_impl.h"
#include "source/extensions/health_checkers/http/health_checker_impl.h"
#include "source/extensions/health_checkers/tcp/health_checker_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/health_checker_test_base.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/transport_socket_match.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace {

envoy::config::core::v3::HealthCheck createGrpcHealthCheckConfig() {
  envoy::config::core::v3::HealthCheck health_check;
  health_check.mutable_timeout()->set_seconds(1);
  health_check.mutable_interval()->set_seconds(1);
  health_check.mutable_unhealthy_threshold()->set_value(2);
  health_check.mutable_healthy_threshold()->set_value(2);
  health_check.mutable_grpc_health_check();
  return health_check;
}

class TestGrpcHealthCheckerImpl : public GrpcHealthCheckerImpl {
public:
  using GrpcHealthCheckerImpl::GrpcHealthCheckerImpl;

  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    auto codec_client = createCodecClient_(conn_data);
    return Http::CodecClientPtr(codec_client);
  };

  // GrpcHealthCheckerImpl
  MOCK_METHOD(Http::CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData&));
};

class GrpcHealthCheckerImplTestBase : public Event::TestUsingSimulatedTime,
                                      public HealthCheckerTestBase {
public:
  struct TestSession {
    TestSession() = default;

    Event::MockTimer* interval_timer_{};
    Event::MockTimer* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockRequestEncoder> request_encoder_;
    Http::ResponseDecoder* stream_response_callbacks_{};
    CodecClientForTest* codec_client_{};
  };

  using TestSessionPtr = std::unique_ptr<TestSession>;

  struct ResponseSpec {
    struct ChunkSpec {
      bool valid;
      std::vector<uint8_t> data;
    };
    static ChunkSpec invalidChunk() {
      ChunkSpec spec;
      spec.valid = false;
      return spec;
    }
    static ChunkSpec invalidPayload(uint8_t flags, bool valid_message) {
      ChunkSpec spec;
      spec.valid = true;
      spec.data = serializeResponse(grpc::health::v1::HealthCheckResponse::SERVING);
      spec.data[0] = flags;
      if (!valid_message) {
        const size_t kGrpcHeaderSize = 5;
        for (size_t i = kGrpcHeaderSize; i < spec.data.size(); i++) {
          // Fill payload with some random data.
          spec.data[i] = i % 256;
        }
      }
      return spec;
    }
    // Null dereference from health check fuzzer
    static ChunkSpec badData() {
      std::string data("\000\000\000\000\0000000", 9);
      std::vector<uint8_t> chunk(data.begin(), data.end());
      ChunkSpec spec;
      spec.valid = true;
      spec.data = chunk;
      return spec;
    }
    static ChunkSpec validFramesThenInvalidFrames() {
      grpc::health::v1::HealthCheckResponse response;
      response.set_status(grpc::health::v1::HealthCheckResponse::SERVING);
      const auto data = Grpc::Common::serializeToGrpcFrame(response);
      std::vector<uint8_t> buffer_vector = std::vector<uint8_t>(data->length(), 0);
      data->copyOut(0, data->length(), &buffer_vector[0]);
      // Invalid frame here
      for (size_t i = 0; i < 6; i++) {
        buffer_vector.push_back(48); // Represents ASCII Character of 0
      }
      ChunkSpec spec;
      spec.valid = true;
      spec.data = buffer_vector;
      return spec;
    }
    static ChunkSpec validChunk(grpc::health::v1::HealthCheckResponse::ServingStatus status) {
      ChunkSpec spec;
      spec.valid = true;
      spec.data = serializeResponse(status);
      return spec;
    }

    static ChunkSpec servingResponse() {
      return validChunk(grpc::health::v1::HealthCheckResponse::SERVING);
    }

    static ChunkSpec notServingResponse() {
      return validChunk(grpc::health::v1::HealthCheckResponse::NOT_SERVING);
    }

    static std::vector<uint8_t>
    serializeResponse(grpc::health::v1::HealthCheckResponse::ServingStatus status) {
      grpc::health::v1::HealthCheckResponse response;
      response.set_status(status);
      const auto data = Grpc::Common::serializeToGrpcFrame(response);
      auto ret = std::vector<uint8_t>(data->length(), 0);
      data->copyOut(0, data->length(), &ret[0]);
      return ret;
    }

    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<ChunkSpec> body_chunks;
    std::vector<std::pair<std::string, std::string>> trailers;
  };

  GrpcHealthCheckerImplTestBase() {
    EXPECT_CALL(*cluster_->info_, features())
        .WillRepeatedly(Return(Upstream::ClusterInfo::Features::HTTP2));
  }

  void allocHealthChecker(const envoy::config::core::v3::HealthCheck& config) {
    health_checker_ = std::make_shared<TestGrpcHealthCheckerImpl>(
        *cluster_, config, dispatcher_, runtime_, random_,
        HealthCheckEventLoggerPtr(event_logger_storage_.release()));
  }

  void addCompletionCallback() {
    health_checker_->addHostCheckCompleteCb(
        [this](HostSharedPtr host, HealthTransition changed_state, HealthState) -> void {
          onHostStatus(host, changed_state);
        });
  }

  void setupHC() {
    const auto config = createGrpcHealthCheckConfig();
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void setupHCWithUnhealthyThreshold(int value) {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_unhealthy_threshold()->set_value(value);
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void setupServiceNameHC(const std::optional<std::string>& authority) {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_grpc_health_check()->set_service_name("service");
    if (authority.has_value()) {
      config.mutable_grpc_health_check()->set_authority(authority.value());
    }
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void setupHCWithHeaders(const absl::flat_hash_map<std::string, std::string> headers_to_add) {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_grpc_health_check()->set_service_name("service");
    for (const auto& pair : headers_to_add) {
      auto header_value_option = config.mutable_grpc_health_check()->add_initial_metadata();
      header_value_option->set_append_action(
          envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
      auto header = header_value_option->mutable_header();
      header->set_key(pair.first);
      header->set_value(pair.second);
    }
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void setupNoReuseConnectionHC() {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_reuse_connection()->set_value(false);
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void setupHealthCheckIntervalOverridesHC() {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_interval()->set_seconds(1);
    config.mutable_unhealthy_interval()->set_seconds(2);
    config.mutable_unhealthy_edge_interval()->set_seconds(3);
    config.mutable_healthy_edge_interval()->set_seconds(4);
    config.mutable_no_traffic_interval()->set_seconds(5);
    config.mutable_interval_jitter()->set_seconds(0);
    config.mutable_unhealthy_threshold()->set_value(3);
    config.mutable_healthy_threshold()->set_value(3);
    allocHealthChecker(config);
    addCompletionCallback();
  }

  void expectSessionCreate() {
    // Expectations are in LIFO order.
    TestSessionPtr new_test_session(new TestSession());
    new_test_session->timeout_timer_ = new Event::MockTimer(&dispatcher_);
    new_test_session->interval_timer_ = new Event::MockTimer(&dispatcher_);
    test_sessions_.emplace_back(std::move(new_test_session));
    expectClientCreate(test_sessions_.size() - 1);
  }

  void expectClientCreate(size_t index) {
    TestSession& test_session = *test_sessions_[index];
    test_session.codec_ = new NiceMock<Http::MockClientConnection>();
    test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
    connection_index_.push_back(index);
    codec_index_.push_back(index);

    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .Times(testing::AnyNumber())
        .WillRepeatedly(InvokeWithoutArgs([&]() -> Network::ClientConnection* {
          const uint32_t index = connection_index_.front();
          connection_index_.pop_front();
          return test_sessions_[index]->client_connection_;
        }));

    EXPECT_CALL(*health_checker_, createCodecClient_(_))
        .WillRepeatedly(
            Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
              const uint32_t index = codec_index_.front();
              codec_index_.pop_front();
              TestSession& test_session = *test_sessions_[index];
              std::shared_ptr<Upstream::MockClusterInfo> cluster{
                  new NiceMock<Upstream::MockClusterInfo>()};
              Event::MockDispatcher dispatcher_;

              test_session.codec_client_ = new CodecClientForTest(
                  Http::CodecType::HTTP1, std::move(conn_data.connection_), test_session.codec_,
                  nullptr, Upstream::makeTestHost(cluster, "tcp://127.0.0.1:9000"), dispatcher_);
              return test_session.codec_client_;
            }));
  }

  void expectStreamCreate(size_t index) {
    test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
    EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
        .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                        ReturnRef(test_sessions_[index]->request_encoder_)));
  }

  // Starts healthchecker and sets up timer expectations, leaving up future specification of
  // healthcheck response for the caller. Useful when there is only one healthcheck attempt
  // performed during test case (but possibly on many hosts).
  void expectHealthchecks(HealthTransition host_changed_state, size_t num_healthchecks) {
    for (size_t i = 0; i < num_healthchecks; i++) {
      cluster_->info_->trafficStats()->upstream_cx_total_.inc();
      expectSessionCreate();
      expectHealthcheckStart(i);
    }
    health_checker_->start();

    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _))
        .Times(num_healthchecks);
    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
        .Times(num_healthchecks)
        .WillRepeatedly(Return(45000));
    for (size_t i = 0; i < num_healthchecks; i++) {
      expectHealthcheckStop(i, 45000);
    }
    EXPECT_CALL(*this, onHostStatus(_, host_changed_state)).Times(num_healthchecks);
  }

  void expectSingleHealthcheck(HealthTransition host_changed_state) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    expectHealthchecks(host_changed_state, 1);
  }

  // Hides timer/stream-related boilerplate of healthcheck start.
  void expectHealthcheckStart(size_t index) {
    expectStreamCreate(index);
    EXPECT_CALL(*test_sessions_[index]->timeout_timer_, enableTimer(_, _));
  }

  // Hides timer-related boilerplate of healthcheck stop.
  void expectHealthcheckStop(size_t index, int interval_ms = 0) {
    if (interval_ms > 0) {
      EXPECT_CALL(*test_sessions_[index]->interval_timer_,
                  enableTimer(std::chrono::milliseconds(interval_ms), _));
    } else {
      EXPECT_CALL(*test_sessions_[index]->interval_timer_, enableTimer(_, _));
    }
    EXPECT_CALL(*test_sessions_[index]->timeout_timer_, disableTimer());
  }

  // Hides host status checking boilerplate when only single host is used in test.
  void expectHostHealthy(bool healthy) {
    const auto host = cluster_->prioritySet().getMockHostSet(0)->hosts_[0];
    if (!healthy) {
      EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      EXPECT_EQ(Host::Health::Unhealthy, host->coarseHealth());
    } else {
      EXPECT_EQ(Host::Health::Healthy, host->coarseHealth());
    }
  }

  void respondServiceStatus(size_t index,
                            grpc::health::v1::HealthCheckResponse::ServingStatus status) {
    respondResponseSpec(index,
                        ResponseSpec{{{":status", "200"}, {"content-type", "application/grpc"}},
                                     {ResponseSpec::validChunk(status)},
                                     {{"grpc-status", "0"}}});
  }

  void respondResponseSpec(size_t index, ResponseSpec&& spec) {
    const bool trailers_empty = spec.trailers.empty();
    const bool end_stream_on_headers = spec.body_chunks.empty() && trailers_empty;
    auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>();
    for (const auto& header : spec.response_headers) {
      response_headers->addCopy(header.first, header.second);
    }
    test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                     end_stream_on_headers);
    for (size_t i = 0; i < spec.body_chunks.size(); i++) {
      const bool end_stream = i == spec.body_chunks.size() - 1 && trailers_empty;
      const auto& chunk = spec.body_chunks[i];
      if (chunk.valid) {
        const auto data = std::make_unique<Buffer::OwnedImpl>(chunk.data.data(), chunk.data.size());
        test_sessions_[index]->stream_response_callbacks_->decodeData(*data, end_stream);
      } else {
        Buffer::OwnedImpl incorrect_data("incorrect");
        test_sessions_[index]->stream_response_callbacks_->decodeData(incorrect_data, end_stream);
      }
    }
    if (!trailers_empty) {
      auto trailers = std::make_unique<Http::TestResponseTrailerMapImpl>();
      for (const auto& header : spec.trailers) {
        trailers->addCopy(header.first, header.second);
      }
      test_sessions_[index]->stream_response_callbacks_->decodeTrailers(std::move(trailers));
    }
  }

  void testSingleHostSuccess(const std::optional<std::string>& authority) {
    std::string expected_host = cluster_->info_->name();
    if (authority.has_value()) {
      expected_host = authority.value();
    }

    setupServiceNameHC(authority);

    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    runHealthCheck(expected_host);
  }

  void runHealthCheck(std::string expected_host) {

    cluster_->info_->trafficStats()->upstream_cx_total_.inc();

    expectSessionCreate();
    expectHealthcheckStart(0);

    EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([&](const Http::RequestHeaderMap& headers, bool) -> Http::Status {
          EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, headers.getContentTypeValue());
          EXPECT_EQ(std::string("/grpc.health.v1.Health/Check"), headers.getPathValue());
          EXPECT_EQ(Http::Headers::get().SchemeValues.Http, headers.getSchemeValue());
          EXPECT_NE(nullptr, headers.Method());
          EXPECT_EQ(expected_host, headers.getHostValue());
          EXPECT_EQ(std::chrono::milliseconds(1000).count(),
                    Envoy::Grpc::Common::getGrpcTimeout(headers).value().count());
          return Http::okStatus();
        }));
    EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeData(_, true))
        .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
          std::vector<Grpc::Frame> decoded_frames;
          Grpc::Decoder decoder;
          ASSERT_TRUE(decoder.decode(data, decoded_frames).ok());
          ASSERT_EQ(1U, decoded_frames.size());
          auto& frame = decoded_frames[0];
          Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
          grpc::health::v1::HealthCheckRequest request;
          ASSERT_TRUE(request.ParseFromZeroCopyStream(&stream));
          EXPECT_EQ("service", request.service());
        }));
    health_checker_->start();

    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
        .WillOnce(Return(45000));
    expectHealthcheckStop(0, 45000);

    // Host state should not be changed (remains healthy).
    EXPECT_CALL(*this, onHostStatus(cluster_->prioritySet().getMockHostSet(0)->hosts_[0],
                                    HealthTransition::Unchanged));
    respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
    expectHostHealthy(true);
  }

  MOCK_METHOD(void, onHostStatus, (HostSharedPtr host, HealthTransition changed_state));

  std::vector<TestSessionPtr> test_sessions_;
  std::shared_ptr<TestGrpcHealthCheckerImpl> health_checker_;
  std::list<uint32_t> connection_index_;
  std::list<uint32_t> codec_index_;
};

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const GrpcHealthCheckerImplTestBase::ResponseSpec& spec, std::ostream* os) {
  (*os) << "(headers{" << absl::StrJoin(spec.response_headers, ",", absl::PairFormatter(":"))
        << "},";
  (*os) << "body{" << absl::StrJoin(spec.body_chunks, ",", [](std::string* out, const auto& spec) {
    absl::StrAppend(out, spec.valid ? "valid" : "invalid", ",{",
                    absl::StrJoin(spec.data, "-",
                                  [](std::string* out, uint8_t byte) {
                                    absl::StrAppend(out, absl::Hex(byte, absl::kZeroPad2));
                                  }),
                    "}");
  }) << "}";
  (*os) << "trailers{" << absl::StrJoin(spec.trailers, ",", absl::PairFormatter(":")) << "})";
}

class GrpcHealthCheckerImplTest : public testing::Test, public GrpcHealthCheckerImplTestBase {};

// Test single host check success.
TEST_F(GrpcHealthCheckerImplTest, Success) { testSingleHostSuccess(std::nullopt); }

TEST_F(GrpcHealthCheckerImplTest, SuccessWithHostname) {
  std::string expected_host = "www.envoyproxy.io";

  setupServiceNameHC(std::nullopt);

  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig health_check_config;
  health_check_config.set_hostname(expected_host);
  auto test_host = std::shared_ptr<Upstream::HostImpl>(*HostImpl::create(
      cluster_->info_, "", *Network::Utility::resolveUrl("tcp://127.0.0.1:80"), nullptr, nullptr, 1,
      std::make_shared<const envoy::config::core::v3::Locality>(), health_check_config, 0,
      envoy::config::core::v3::UNKNOWN));
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  runHealthCheck(expected_host);
}

TEST_F(GrpcHealthCheckerImplTest, SuccessWithHostnameOverridesConfig) {
  std::string expected_host = "www.envoyproxy.io";

  setupServiceNameHC("foo.com");

  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig health_check_config;
  health_check_config.set_hostname(expected_host);
  auto test_host = std::shared_ptr<Upstream::HostImpl>(*HostImpl::create(
      cluster_->info_, "", *Network::Utility::resolveUrl("tcp://127.0.0.1:80"), nullptr, nullptr, 1,
      std::make_shared<const envoy::config::core::v3::Locality>(), health_check_config, 0,
      envoy::config::core::v3::UNKNOWN));
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {test_host};
  runHealthCheck(expected_host);
}

// Test single host check success with custom authority.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithCustomAuthority) {
  const std::string authority = "www.envoyproxy.io";
  testSingleHostSuccess(authority);
}

// Test single host check success with additional headers.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithAdditionalHeaders) {
  const std::string ENVOY_OK_KEY = "x-envoy-ok";
  const std::string ENVOY_OK_VAL = "ok";
  const std::string ENVOY_COOL_KEY = "x-envoy-cool";
  const std::string ENVOY_COOL_VAL = "cool";
  const std::string ENVOY_AWESOME_KEY = "x-envoy-awesome";
  const std::string ENVOY_AWESOME_VAL = "awesome";
  const std::string USER_AGENT_KEY = "user-agent";
  const std::string USER_AGENT_VAL = "CoolEnvoy/HC";
  const std::string PROTOCOL_KEY = "x-protocol";
  const std::string PROTOCOL_VAL = "%PROTOCOL%";
  const std::string DOWNSTREAM_LOCAL_ADDRESS_KEY = "x-downstream-local-address";
  const std::string DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT_KEY =
      "x-downstream-local-address-without-port";
  const std::string DOWNSTREAM_REMOTE_ADDRESS_KEY = "x-downstream-remote-address";
  const std::string DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_KEY =
      "x-downstream-remote-address-without-port";
  const std::string START_TIME_KEY = "x-start-time";
  const std::string UPSTREAM_METADATA_KEY = "x-upstream-metadata";

  setupHCWithHeaders(
      {{ENVOY_OK_KEY, ENVOY_OK_VAL},
       {ENVOY_COOL_KEY, ENVOY_COOL_VAL},
       {ENVOY_AWESOME_KEY, ENVOY_AWESOME_VAL},
       {USER_AGENT_KEY, USER_AGENT_VAL},
       {PROTOCOL_KEY, PROTOCOL_VAL},
       {DOWNSTREAM_LOCAL_ADDRESS_KEY, "%DOWNSTREAM_LOCAL_ADDRESS%"},
       {DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT_KEY, "%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%"},
       {DOWNSTREAM_REMOTE_ADDRESS_KEY, "%DOWNSTREAM_REMOTE_ADDRESS%"},
       {DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_KEY, "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"},
       {START_TIME_KEY, "%START_TIME(%s.%9f)%"},
       {UPSTREAM_METADATA_KEY, "%UPSTREAM_METADATA(namespace:key)%"}});

  auto metadata = TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
      R"EOF(
        filter_metadata:
          namespace:
            key: value
      )EOF");

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80", metadata)};

  cluster_->info_->trafficStats()->upstream_cx_total_.inc();

  expectSessionCreate();
  expectHealthcheckStart(0);

  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const Http::RequestHeaderMap& headers, bool) -> Http::Status {
        EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc, headers.getContentTypeValue());
        EXPECT_EQ(std::string("/grpc.health.v1.Health/Check"), headers.getPathValue());
        EXPECT_EQ(Http::Headers::get().SchemeValues.Http, headers.getSchemeValue());
        EXPECT_NE(nullptr, headers.Method());
        EXPECT_EQ(cluster_->info_->name(), headers.getHostValue());
        EXPECT_EQ(ENVOY_OK_VAL,
                  headers.get(Http::LowerCaseString(ENVOY_OK_KEY))[0]->value().getStringView());
        EXPECT_EQ(ENVOY_COOL_VAL,
                  headers.get(Http::LowerCaseString(ENVOY_COOL_KEY))[0]->value().getStringView());
        EXPECT_EQ(
            ENVOY_AWESOME_VAL,
            headers.get(Http::LowerCaseString(ENVOY_AWESOME_KEY))[0]->value().getStringView());
        EXPECT_EQ(USER_AGENT_VAL, headers.getUserAgentValue());
        EXPECT_EQ("HTTP/2",
                  headers.get(Http::LowerCaseString(PROTOCOL_KEY))[0]->value().getStringView());
        EXPECT_EQ("127.0.0.1:0",
                  headers.get(Http::LowerCaseString(DOWNSTREAM_LOCAL_ADDRESS_KEY))[0]
                      ->value()
                      .getStringView());
        EXPECT_EQ("127.0.0.1",
                  headers.get(Http::LowerCaseString(DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT_KEY))[0]
                      ->value()
                      .getStringView());
        EXPECT_EQ("127.0.0.1:0",
                  headers.get(Http::LowerCaseString(DOWNSTREAM_REMOTE_ADDRESS_KEY))[0]
                      ->value()
                      .getStringView());
        EXPECT_EQ("127.0.0.1",
                  headers.get(Http::LowerCaseString(DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT_KEY))[0]
                      ->value()
                      .getStringView());
        Envoy::DateFormatter date_formatter("%s.%9f");
        std::string current_start_time =
            date_formatter.fromTime(dispatcher_.timeSource().systemTime());
        EXPECT_EQ(current_start_time,
                  headers.get(Http::LowerCaseString(START_TIME_KEY))[0]->value().getStringView());
        EXPECT_EQ(
            "value",
            headers.get(Http::LowerCaseString(UPSTREAM_METADATA_KEY))[0]->value().getStringView());
        EXPECT_EQ(std::chrono::milliseconds(1000).count(),
                  Envoy::Grpc::Common::getGrpcTimeout(headers).value().count());
        return Http::okStatus();
      }));
  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        std::vector<Grpc::Frame> decoded_frames;
        Grpc::Decoder decoder;
        ASSERT_TRUE(decoder.decode(data, decoded_frames).ok());
        ASSERT_EQ(1U, decoded_frames.size());
        auto& frame = decoded_frames[0];
        Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
        grpc::health::v1::HealthCheckRequest request;
        ASSERT_TRUE(request.ParseFromZeroCopyStream(&stream));
        EXPECT_EQ("service", request.service());
      }));

  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  expectHealthcheckStop(0, 45000);

  // Host state should not be changed (remains healthy).
  EXPECT_CALL(*this, onHostStatus(cluster_->prioritySet().getMockHostSet(0)->hosts_[0],
                                  HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test host check success when gRPC response payload is split between several incoming data chunks.
TEST_F(GrpcHealthCheckerImplTest, SuccessResponseSplitBetweenChunks) {
  setupServiceNameHC(std::nullopt);
  expectSingleHealthcheck(HealthTransition::Unchanged);

  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":status", "200"},
          {"content-type", "application/grpc"},
      });
  test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), false);

  grpc::health::v1::HealthCheckResponse response;
  response.set_status(grpc::health::v1::HealthCheckResponse::SERVING);
  auto data = Grpc::Common::serializeToGrpcFrame(response);

  const char* raw_data = static_cast<char*>(data->linearize(data->length()));
  const uint64_t chunk_size = data->length() / 5;
  for (uint64_t offset = 0; offset < data->length(); offset += chunk_size) {
    const uint64_t effective_size = std::min(chunk_size, data->length() - offset);
    const auto chunk = std::make_unique<Buffer::OwnedImpl>(raw_data + offset, effective_size);
    test_sessions_[0]->stream_response_callbacks_->decodeData(*chunk, false);
  }

  auto trailers = std::make_unique<Http::TestResponseTrailerMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{{"grpc-status", "0"}});
  test_sessions_[0]->stream_response_callbacks_->decodeTrailers(std::move(trailers));

  expectHostHealthy(true);
}

// Test host check success with multiple hosts.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithMultipleHosts) {
  setupHC();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

  expectHealthchecks(HealthTransition::Unchanged, 2);

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  respondServiceStatus(1, grpc::health::v1::HealthCheckResponse::SERVING);
  EXPECT_EQ(Host::Health::Healthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
  EXPECT_EQ(Host::Health::Healthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[1]->coarseHealth());
}

// Test host check success with multiple hosts across multiple priorities.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithMultipleHostSets) {
  setupHC();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(1)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

  expectHealthchecks(HealthTransition::Unchanged, 2);

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  respondServiceStatus(1, grpc::health::v1::HealthCheckResponse::SERVING);
  EXPECT_EQ(Host::Health::Healthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
  EXPECT_EQ(Host::Health::Healthy,
            cluster_->prioritySet().getMockHostSet(1)->hosts_[0]->coarseHealth());
}

// Test stream-level watermarks does not interfere with health check.
TEST_F(GrpcHealthCheckerImplTest, StreamReachesWatermarkDuringCheck) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Unchanged);

  test_sessions_[0]->request_encoder_.stream_.runHighWatermarkCallbacks();
  test_sessions_[0]->request_encoder_.stream_.runLowWatermarkCallbacks();

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test connection-level watermarks does not interfere with health check.
TEST_F(GrpcHealthCheckerImplTest, ConnectionReachesWatermarkDuringCheck) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Unchanged);

  test_sessions_[0]->client_connection_->runHighWatermarkCallbacks();
  test_sessions_[0]->client_connection_->runLowWatermarkCallbacks();

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test health check on host without traffic sets larger unconfigurable interval for the next check.
TEST_F(GrpcHealthCheckerImplTest, SuccessNoTraffic) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  // Default healthcheck interval for hosts without traffic is 60 seconds.
  expectHealthcheckStop(0, 60000);
  // Host state should not be changed (remains healthy).
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test first successful check immediately makes failed host available (without 2nd probe).
TEST_F(GrpcHealthCheckerImplTest, SuccessStartFailedSuccessFirst) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::PENDING_ACTIVE_HC);

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).WillOnce(Return(500));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _));
  expectHealthcheckStop(0, 500);
  // Fast success immediately moves us to healthy.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, true));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::PENDING_ACTIVE_HC));
}

// Test host recovery after first failed check requires several successful checks.
TEST_F(GrpcHealthCheckerImplTest, SuccessStartFailedFailFirst) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::PENDING_ACTIVE_HC);

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  // Failing first disables fast success.
  expectHealthcheckStop(0);
  // Host was unhealthy from the start, but we expect a state change due to the pending active hc
  // flag changing.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);
  expectHostHealthy(false);
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::PENDING_ACTIVE_HC));

  // Next successful healthcheck does not move host int healthy state (because we configured
  // healthchecker this way).
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Host still unhealthy, need yet another healthcheck.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);

  // 2nd successful healthcheck renders host healthy.
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Verify functionality when a host is removed inline with a failure via RPC that was proceeded
// by a GOAWAY.
TEST_F(GrpcHealthCheckerImplTest, GrpcHealthFailViaRpcRemoveHostInCallback) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed))
      .WillOnce(Invoke([&](HostSharedPtr host, HealthTransition) {
        cluster_->prioritySet().getMockHostSet(0)->hosts_ = {};
        cluster_->prioritySet().runUpdateCallbacks(0, {}, {host});
      }));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);
}

// Verify functionality when a host is removed inline with a failure via an error GOAWAY.
TEST_F(GrpcHealthCheckerImplTest, GrpcHealthFailViaGoawayRemoveHostInCallback) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed))
      .WillOnce(Invoke([&](HostSharedPtr host, HealthTransition) {
        cluster_->prioritySet().getMockHostSet(0)->hosts_ = {};
        cluster_->prioritySet().runUpdateCallbacks(0, {}, {host});
      }));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::Other);
}

// Verify functionality when a host is removed inline with by a bad RPC response.
TEST_F(GrpcHealthCheckerImplTest, GrpcHealthFailViaBadResponseRemoveHostInCallback) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed))
      .WillOnce(Invoke([&](HostSharedPtr host, HealthTransition) {
        cluster_->prioritySet().getMockHostSet(0)->hosts_ = {};
        cluster_->prioritySet().runUpdateCallbacks(0, {}, {host});
      }));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  std::unique_ptr<Http::TestResponseHeaderMapImpl> response_headers(
      new Http::TestResponseHeaderMapImpl{{":status", "500"}});
  test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), false);
}

// Test host recovery after explicit check failure requires several successful checks.
TEST_F(GrpcHealthCheckerImplTest, GrpcHealthFail) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  // Explicit healthcheck failure immediately renders host unhealthy.
  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);
  expectHostHealthy(false);

  // Next, we need 2 successful checks for host to become available again.
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Host still considered unhealthy.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);

  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Host should has become healthy.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test disconnects produce network-type failures which does not lead to immediate unhealthy state.
TEST_F(GrpcHealthCheckerImplTest, Disconnect) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Network-type healthcheck failure should make host unhealthy only after 2nd event in a row.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(true);

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Now, host should be unhealthy.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(false);
}

TEST_F(GrpcHealthCheckerImplTest, Timeout) {
  setupHCWithUnhealthyThreshold(1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();

  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Unhealthy threshold is 1 so first timeout causes unhealthy
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  test_sessions_[0]->timeout_timer_->invokeCallback();
  expectHostHealthy(false);
}

// Test timeouts produce network-type failures which does not lead to immediate unhealthy state.
TEST_F(GrpcHealthCheckerImplTest, DoubleTimeout) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();

  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Timeouts are considered network failures and make host unhealthy also after 2nd event.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  test_sessions_[0]->timeout_timer_->invokeCallback();
  expectHostHealthy(true);

  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  // Close connection. Timeouts and connection closes counts together.
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(false);
}

// Test adding and removal of hosts starts and closes healthcheck sessions.
TEST_F(GrpcHealthCheckerImplTest, DynamicAddAndRemove) {
  setupHC();
  health_checker_->start();

  expectSessionCreate();
  expectStreamCreate(0);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  EXPECT_CALL(*test_sessions_[0]->client_connection_,
              close(Network::ConnectionCloseType::Abort, _));
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

TEST_F(GrpcHealthCheckerImplTest, HealthCheckIntervals) {
  setupHealthCheckIntervalOverridesHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://128.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  health_checker_->start();

  // First check should respect no_traffic_interval setting.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  cluster_->info_->trafficStats()->upstream_cx_total_.inc();

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Follow up successful checks should respect interval setting.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Follow up successful checks should respect interval setting.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // A logical failure is not considered a network failure, therefore the unhealthy threshold is
  // ignored and health state changes immediately. Since the threshold is ignored, next health
  // check respects "unhealthy_interval".
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Subsequent failing checks should respect unhealthy_interval.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Subsequent failing checks should respect unhealthy_interval.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // When transitioning to a successful state, checks should respect healthy_edge_interval. Health
  // state should be delayed pending healthy threshold.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // After the healthy threshold is reached, health state should change while checks should respect
  // the default interval.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Subsequent checks shouldn't change the state.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // First failed check after a run o successful ones should respect unhealthy_edge_interval. A
  // timeout, being a network type failure, should respect unhealthy threshold before changing the
  // health state.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->invokeCallback();

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(3000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->invokeCallback();

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Subsequent failing checks should respect unhealthy_interval. As the unhealthy threshold is
  // reached, health state should also change.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->invokeCallback();

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Remaining failing checks shouldn't change the state.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(2000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->invokeCallback();

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // When transitioning to a successful state, checks should respect healthy_edge_interval.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // After the healthy threshold is reached, health state should change while checks should respect
  // the default interval.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logAddHealthy(_, _, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_, _));
  // Needed after a response is sent.
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  // Subsequent checks shouldn't change the state.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
}

// Test connection close between checks affects nothing.
TEST_F(GrpcHealthCheckerImplTest, RemoteCloseBetweenChecks) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // Connection closed between checks - nothing happens, just re-create client.
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test that we close connections on a healthy check when reuse_connection is false.
TEST_F(GrpcHealthCheckerImplTest, DontReuseConnectionBetweenChecks) {
  setupNoReuseConnectionHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // A new client is created because we close the connection ourselves.
  // See GrpcHealthCheckerImplTest.RemoteCloseBetweenChecks for how this works when the remote end
  // closes the connection.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test that we close connections when a timeout occurs and reuse_connection is false.
TEST_F(GrpcHealthCheckerImplTest, DontReuseConnectionTimeout) {
  setupNoReuseConnectionHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Timeouts are considered network failures and make host unhealthy also after 2nd event.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  test_sessions_[0]->timeout_timer_->invokeCallback();
  expectHostHealthy(true);

  // A new client is created because we close the connection
  // when a timeout occurs and connection reuse is disabled.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test that we close connections when a stream reset occurs and reuse_connection is false.
TEST_F(GrpcHealthCheckerImplTest, DontReuseConnectionStreamReset) {
  setupNoReuseConnectionHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Resets are considered network failures and make host unhealthy also after 2nd event.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  test_sessions_[0]->request_encoder_.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  expectHostHealthy(true);

  // A new client is created because we close the connection
  // when a stream reset occurs and connection reuse is disabled.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test UNKNOWN health status is considered unhealthy.
TEST_F(GrpcHealthCheckerImplTest, GrpcFailUnknown) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::UNKNOWN);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// This used to cause a null dereference
TEST_F(GrpcHealthCheckerImplTest, GrpcFailNullBytes) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  respondResponseSpec(0, ResponseSpec{{{":status", "200"}, {"content-type", "application/grpc"}},
                                      {GrpcHealthCheckerImplTest::ResponseSpec::badData()},
                                      {}});
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// This used to cause a null dereference
TEST_F(GrpcHealthCheckerImplTest, GrpcValidFramesThenInvalidFrames) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  respondResponseSpec(
      0, ResponseSpec{{{":status", "200"}, {"content-type", "application/grpc"}},
                      {GrpcHealthCheckerImplTest::ResponseSpec::validFramesThenInvalidFrames()},
                      {}});
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// Test SERVICE_UNKNOWN health status is considered unhealthy.
TEST_F(GrpcHealthCheckerImplTest, GrpcFailServiceUnknown) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVICE_UNKNOWN);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// Test non existent health status enum is considered unhealthy.
TEST_F(GrpcHealthCheckerImplTest, GrpcFailUnknownHealthStatus) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));

  respondServiceStatus(0, static_cast<grpc::health::v1::HealthCheckResponse::ServingStatus>(999));
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// Test receiving GOAWAY (error) is interpreted as connection close event.
TEST_F(GrpcHealthCheckerImplTest, GoAwayErrorProbeInProgress) {
  // FailureType::Network will be issued, it will render host unhealthy only if unhealthy_threshold
  // is reached.
  setupHCWithUnhealthyThreshold(1);
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));

  // GOAWAY with non-NO_ERROR code will result in a healthcheck failure
  // and the connection closing.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::Other);

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(Host::Health::Unhealthy,
            cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->coarseHealth());
}

// Test receiving GOAWAY (no error) is handled gracefully while a check is in progress.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgress) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));

  // GOAWAY with NO_ERROR code during check should be handle gracefully.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // GOAWAY should cause a new connection to be created.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test receiving GOAWAY (no error) closes connection after an in progress probe times outs.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgressTimeout) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Unhealthy threshold is 1 so first timeout causes unhealthy
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));

  // GOAWAY during check should be handled gracefully.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  expectHostHealthy(true);

  test_sessions_[0]->timeout_timer_->invokeCallback();
  expectHostHealthy(false);

  // GOAWAY should cause a new connection to be created.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Healthy threshold is 2, so the we'ere pending a state change.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);
}

// Test receiving GOAWAY (no error) closes connection after an unexpected stream reset.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgressStreamReset) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Unhealthy threshold is 1 so first stream reset causes unhealthy
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));

  // GOAWAY during check should be handled gracefully.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  expectHostHealthy(true);

  test_sessions_[0]->request_encoder_.stream_.resetStream(Http::StreamResetReason::RemoteReset);
  expectHostHealthy(false);

  // GOAWAY should cause a new connection to be created.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Healthy threshold is 2, so the we'ere pending a state change.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);
}

// Test receiving GOAWAY (no error) closes connection after a bad response.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgressBadResponse) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Unhealthy threshold is 1 so first bad response causes unhealthy
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));

  // GOAWAY during check should be handled gracefully.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  expectHostHealthy(true);

  respondResponseSpec(0, ResponseSpec{{{":status", "200"}, {"content-type", "application/grpc"}},
                                      {ResponseSpec::invalidChunk()},
                                      {}});
  expectHostHealthy(false);

  // GOAWAY should cause a new connection to be created.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Healthy threshold is 2, so the we'ere pending a state change.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);
}

// Test receiving GOAWAY (no error) and a connection close.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgressConnectionClose) {
  setupHCWithUnhealthyThreshold(/*threshold=*/1);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  health_checker_->start();

  expectHealthcheckStop(0);
  // Unhealthy threshold is 1 so first bad response causes unhealthy
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Changed));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));

  // GOAWAY during check should be handled gracefully.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);
  expectHostHealthy(true);

  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(false);

  // GOAWAY should cause a new connection to be created.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Healthy threshold is 2, so the we'ere pending a state change.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::ChangePending));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);
}

// Test receiving GOAWAY between checks affects nothing.
TEST_F(GrpcHealthCheckerImplTest, GoAwayBetweenChecks) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // GOAWAY between checks should go unnoticed.
  test_sessions_[0]->codec_client_->raiseGoAway(Http::GoAwayErrorCode::NoError);

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->invokeCallback();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, HealthTransition::Unchanged));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

class BadResponseGrpcHealthCheckerImplTest
    : public testing::TestWithParam<GrpcHealthCheckerImplTest::ResponseSpec>,
      public GrpcHealthCheckerImplTestBase {};

INSTANTIATE_TEST_SUITE_P(
    BadResponse, BadResponseGrpcHealthCheckerImplTest,
    testing::ValuesIn(std::vector<GrpcHealthCheckerImplTest::ResponseSpec>{
        // Non-200 response.
        {
            {{":status", "500"}},
            {},
            {},
        },
        // Non-200 response with gRPC status.
        {
            {{":status", "500"}, {"grpc-status", "2"}},
            {},
            {},
        },
        // Missing content-type.
        {
            {{":status", "200"}},
            {},
            {},
        },
        // End stream on response headers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {},
            {},
        },
        // Non-OK gRPC status in headers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}, {"grpc-status", "2"}},
            {},
            {},
        },
        // Non-OK gRPC status
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"grpc-status", "2"}},
        },
        // Missing body.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}, {"grpc-status", "0"}},
            {},
            {},
        },
        // Compressed body.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidPayload(Grpc::GRPC_FH_COMPRESSED,
                                                                     true)},
            {},
        },
        // Invalid proto message.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidPayload(Grpc::GRPC_FH_DEFAULT, false)},
            {},
        },
        // Duplicate response.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse(),
             GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {},
        },
        // Invalid response.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidChunk()},
            {},
        },
        // No trailers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {},
        },
        // No gRPC status in trailer.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"some-header", "1"}},
        },
        // Invalid gRPC status.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"grpc-status", "invalid"}},
        },
    }));

// Test different cases of invalid gRPC response makes host unhealthy.
TEST_P(BadResponseGrpcHealthCheckerImplTest, GrpcBadResponse) {
  setupHC();
  expectSingleHealthcheck(HealthTransition::Changed);
  EXPECT_CALL(event_logger_, logUnhealthy(_, _, _, true, _));
  EXPECT_CALL(event_logger_, logEjectUnhealthy(_, _, _, _));

  ResponseSpec spec = GetParam();
  respondResponseSpec(0, std::move(spec));
  expectHostHealthy(false);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
