#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3/admission_control.pb.validate.h"
#include "envoy/grpc/status.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/admission_control/admission_control.h"
#include "source/extensions/filters/http/admission_control/evaluators/response_evaluator.h"
#include "source/extensions/filters/http/admission_control/thread_local_controller.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {
namespace {

using RequestData = ThreadLocalController::RequestData;

class MockThreadLocalController : public ThreadLocal::ThreadLocalObject,
                                  public ThreadLocalController {
public:
  MOCK_METHOD(RequestData, requestCounts, ());
  MOCK_METHOD(void, recordSuccess, ());
  MOCK_METHOD(void, recordFailure, ());
  MOCK_METHOD(uint32_t, averageRps, (), (const));
};

class MockResponseEvaluator : public ResponseEvaluator {
public:
  MOCK_METHOD(bool, isHttpSuccess, (uint64_t code), (const));
  MOCK_METHOD(bool, isGrpcSuccess, (uint32_t status), (const));
};

class TestConfig : public AdmissionControlFilterConfig {
public:
  TestConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
             Random::RandomGenerator& random, Stats::Scope& scope,
             ThreadLocal::TypedSlotPtr<ThreadLocalControllerImpl>&& tls,
             MockThreadLocalController& controller, std::shared_ptr<ResponseEvaluator> evaluator)
      : AdmissionControlFilterConfig(proto_config, runtime, random, scope, std::move(tls),
                                     std::move(evaluator)),
        controller_(controller) {}
  ThreadLocalController& getController() const override { return controller_; }

private:
  MockThreadLocalController& controller_;
};

class AdmissionControlTest : public testing::Test {
public:
  AdmissionControlTest() = default;

  std::shared_ptr<AdmissionControlFilterConfig> makeConfig(const std::string& yaml) {
    AdmissionControlProto proto;
    TestUtility::loadFromYamlAndValidate(yaml, proto);
    auto tls = ThreadLocal::TypedSlot<ThreadLocalControllerImpl>::makeUnique(
        context_.server_factory_context_.threadLocal());
    evaluator_ = std::make_shared<MockResponseEvaluator>();

    return std::make_shared<TestConfig>(proto, runtime_, random_, scope_, std::move(tls),
                                        controller_, evaluator_);
  }

  void setupFilter(std::shared_ptr<AdmissionControlFilterConfig> config) {
    filter_ = std::make_shared<AdmissionControlFilter>(config, "test_prefix.");
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void sampleGrpcRequest(const Grpc::Status::WellKnownGrpcStatus status) {
    Http::TestResponseHeaderMapImpl headers{{"content-type", "application/grpc"},
                                            {"grpc-status", std::to_string(enumToInt(status))}};
    filter_->encodeHeaders(headers, true);
  }

  void sampleGrpcRequestTrailer(const Grpc::Status::WellKnownGrpcStatus status) {
    Http::TestResponseHeaderMapImpl headers{{"content-type", "application/grpc"},
                                            {":status", "200"}};
    filter_->encodeHeaders(headers, false);
    Http::TestResponseTrailerMapImpl trailers{{"grpc-message", "foo"},
                                              {"grpc-status", std::to_string(enumToInt(status))}};
    filter_->encodeTrailers(trailers);
  }

  void sampleHttpRequest(const std::string& http_error_code) {
    Http::TestResponseHeaderMapImpl headers{{":status", http_error_code}};
    filter_->encodeHeaders(headers, true);
  }

  void verifyProbabilities(int success_rate, double expected_rejection_probability) {
    // Success rate will be the same as the number of successful requests if the total request count
    // is 100.
    constexpr int total_request_count = 100;
    EXPECT_CALL(controller_, requestCounts())
        .WillRepeatedly(Return(RequestData(total_request_count, success_rate)));
    EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));

    Http::TestRequestHeaderMapImpl request_headers;
    uint32_t rejection_count = 0;
    // Assuming 4 significant figures in rejection probability calculation.
    const auto accuracy = 1e4;
    for (int i = 0; i < accuracy; ++i) {
      EXPECT_CALL(random_, random()).WillRepeatedly(Return(i));
      if (filter_->decodeHeaders(request_headers, true) != Http::FilterHeadersStatus::Continue) {
        ++rejection_count;
      }
    }

    EXPECT_NEAR(static_cast<double>(rejection_count) / accuracy, expected_rejection_probability,
                0.01);
  }

protected:
  std::string stats_prefix_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::shared_ptr<AdmissionControlFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<MockThreadLocalController> controller_;
  std::shared_ptr<MockResponseEvaluator> evaluator_;
  const std::string default_yaml_{R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression:
  default_value: 1.0
  runtime_key: "foo.aggression"
max_rejection_probability:
  default_value:
    value: 100.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF"};
};

// Ensure the filter can be disabled/enabled via runtime.
TEST_F(AdmissionControlTest, FilterRuntimeOverride) {
  const std::string yaml = R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression:
  default_value: 1.0
  runtime_key: "foo.aggression"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  // "Disable" the filter via runtime.
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.enabled", true)).WillRepeatedly(Return(false));

  // The filter is bypassed via runtime.
  EXPECT_CALL(controller_, requestCounts()).Times(0);
  EXPECT_CALL(controller_, averageRps()).Times(0);

  // We expect no rejections.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Ensure the filter disregards healthcheck traffic.
TEST_F(AdmissionControlTest, DisregardHealthChecks) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

  // We do not make admission decisions for health checks, so we expect no lookup of request success
  // counts.
  EXPECT_CALL(controller_, requestCounts()).Times(0);
  EXPECT_CALL(controller_, averageRps()).Times(0);

  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
}

// Validate simple HTTP failure case.
TEST_F(AdmissionControlTest, HttpFailureBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isHttpSuccess(500)).WillRepeatedly(Return(false));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleHttpRequest("500");

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple HTTP success case.
TEST_F(AdmissionControlTest, HttpSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isHttpSuccess(200)).WillRepeatedly(Return(true));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleHttpRequest("200");

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);
}

// Validate simple gRPC failure case.
TEST_F(AdmissionControlTest, GrpcFailureBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(7)).WillRepeatedly(Return(false));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequest(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple gRPC success case with status in the trailer.
TEST_F(AdmissionControlTest, GrpcSuccessBehaviorTrailer) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequestTrailer(Grpc::Status::WellKnownGrpcStatus::Ok);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);
}

// Validate simple gRPC failure case with status in the trailer.
TEST_F(AdmissionControlTest, GrpcFailureBehaviorTrailer) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(7)).WillRepeatedly(Return(false));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequestTrailer(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple gRPC success case.
TEST_F(AdmissionControlTest, GrpcSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(99));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequest(Grpc::Status::WellKnownGrpcStatus::Ok);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);
}

// Validate rejection probabilities.
TEST_F(AdmissionControlTest, RejectionProbability) {
  std::string yaml = R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
sr_threshold:
  default_value:
    value: 100.0
  runtime_key: "foo.threshold"
aggression:
  default_value: 1.0
  runtime_key: "foo.aggression"
max_rejection_probability:
  default_value:
    value: 80.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  verifyProbabilities(100 /* success rate */, 0.0 /* expected rejection probability */);
  verifyProbabilities(95, 0.05);
  verifyProbabilities(75, 0.25);

  // Increase aggression and expect higher rejection probabilities for the same values.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 1.0)).WillRepeatedly(Return(2.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.threshold", 100.0)).WillRepeatedly(Return(100.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 80.0))
      .WillRepeatedly(Return(80.0));
  verifyProbabilities(100, 0.0);
  verifyProbabilities(95, 0.22);
  verifyProbabilities(75, 0.5);

  // Lower the success rate threshold and expect the rejections to begin at a lower SR and increase
  // from there.
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 1.0)).WillRepeatedly(Return(1.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.threshold", 100.0)).WillRepeatedly(Return(95.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 80.0))
      .WillRepeatedly(Return(80.0));
  verifyProbabilities(100, 0.0);
  verifyProbabilities(98, 0.0);
  verifyProbabilities(95, 0.0);
  verifyProbabilities(90, 0.05);
  verifyProbabilities(75, 0.20);
  verifyProbabilities(50, 0.46);

  // Validate max rejection probability
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 1.0)).WillRepeatedly(Return(1.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.threshold", 100.0)).WillRepeatedly(Return(100.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 80.0))
      .WillRepeatedly(Return(10.0));

  verifyProbabilities(100, 0.0);
  verifyProbabilities(95, 0.05);
  verifyProbabilities(80, 0.1);
  verifyProbabilities(0, 0.1);
}

// Validate RPS threshold.
TEST_F(AdmissionControlTest, RpsThreshold) {
  std::string yaml = R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
aggression:
  default_value: 1.0
  runtime_key: "foo.aggression"
rps_threshold:
  default_value: 0
  runtime_key: "foo.rps_threshold"
max_rejection_probability:
  default_value:
    value: 100.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.rps_threshold", 0)).WillRepeatedly(Return(10));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(1));
  EXPECT_CALL(controller_, requestCounts()).Times(0);

  // Continue expected.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("foo.rps_threshold", 0)).WillRepeatedly(Return(10));
  EXPECT_CALL(controller_, averageRps()).WillRepeatedly(Return(100));

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isHttpSuccess(500)).WillRepeatedly(Return(false));

  // StopIteration expected.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleHttpRequest("500");

  TestUtility::waitForCounterEq(store_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate max rejection probability.
TEST_F(AdmissionControlTest, MaxRejectionProbability) {
  std::string yaml = R"EOF(
enabled:
  default_value: true
  runtime_key: "foo.enabled"
sampling_window: 10s
sr_threshold:
  default_value:
    value: 100.0
  runtime_key: "foo.threshold"
aggression:
  default_value: 1.0
  runtime_key: "foo.aggression"
max_rejection_probability:
  default_value:
    value: 80.0
  runtime_key: "foo.max_rejection_probability"
success_criteria:
  http_criteria:
  grpc_criteria:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  // Validate max rejection probability
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.aggression", 1.0)).WillRepeatedly(Return(1.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.threshold", 100.0)).WillRepeatedly(Return(100.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("foo.max_rejection_probability", 80.0))
      .WillRepeatedly(Return(10.0));

  verifyProbabilities(100, 0.0);
  verifyProbabilities(95, 0.05);
  verifyProbabilities(80, 0.1);
  verifyProbabilities(0, 0.1);
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
