#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"
#include "envoy/grpc/status.h"

#include "common/common/enum_to_int.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/http/admission_control/admission_control.h"
#include "extensions/filters/http/admission_control/evaluators/response_evaluator.h"
#include "extensions/filters/http/admission_control/thread_local_controller.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
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
};

class MockResponseEvaluator : public ResponseEvaluator {
public:
  MOCK_METHOD(bool, isHttpSuccess, (uint64_t code), (const));
  MOCK_METHOD(bool, isGrpcSuccess, (uint32_t status), (const));
};

class TestConfig : public AdmissionControlFilterConfig {
public:
  TestConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
             Random::RandomGenerator& random, Stats::Scope& scope, ThreadLocal::SlotPtr&& tls,
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
    auto tls = context_.threadLocal().allocateSlot();
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

protected:
  std::string stats_prefix_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
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
aggression_coefficient:
  default_value: 1.0
  runtime_key: "foo.aggression"
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
aggression_coefficient:
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
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isHttpSuccess(500)).WillRepeatedly(Return(false));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleHttpRequest("500");

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple HTTP success case.
TEST_F(AdmissionControlTest, HttpSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isHttpSuccess(200)).WillRepeatedly(Return(true));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleHttpRequest("200");

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);
}

// Validate simple gRPC failure case.
TEST_F(AdmissionControlTest, GrpcFailureBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(7)).WillRepeatedly(Return(false));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequest(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple gRPC success case with status in the trailer.
TEST_F(AdmissionControlTest, GrpcSuccessBehaviorTrailer) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequestTrailer(Grpc::Status::WellKnownGrpcStatus::Ok);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);
}

// Validate simple gRPC failure case with status in the trailer.
TEST_F(AdmissionControlTest, GrpcFailureBehaviorTrailer) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 0)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(7)).WillRepeatedly(Return(false));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequestTrailer(Grpc::Status::WellKnownGrpcStatus::PermissionDenied);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple gRPC success case.
TEST_F(AdmissionControlTest, GrpcSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestCounts()).WillRepeatedly(Return(RequestData(100, 100)));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequest(Grpc::Status::WellKnownGrpcStatus::Ok);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
