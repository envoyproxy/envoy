#include <chrono>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

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

class MockThreadLocalController : public ThreadLocal::ThreadLocalObject,
                                  public ThreadLocalController {
public:
  MockThreadLocalController() = default;
  MOCK_METHOD(uint32_t, requestTotalCount, (), (override));
  MOCK_METHOD(uint32_t, requestSuccessCount, (), (override));
  MOCK_METHOD(void, recordSuccess, (), (override));
  MOCK_METHOD(void, recordFailure, (), (override));
};

class MockResponseEvaluator : public ResponseEvaluator {
public:
  MockResponseEvaluator() = default;
  MOCK_METHOD(bool, isHttpSuccess, (uint64_t code), (const, override));
  MOCK_METHOD(bool, isGrpcSuccess, (uint32_t status), (const, override));
};

class TestConfig : public AdmissionControlFilterConfig {
public:
  TestConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
             TimeSource& time_source, Runtime::RandomGenerator& random, Stats::Scope& scope,
             ThreadLocal::SlotPtr&& tls, MockThreadLocalController& controller,
             std::shared_ptr<ResponseEvaluator> evaluator)
      : AdmissionControlFilterConfig(proto_config, runtime, time_source, random, scope,
                                     std::move(tls), evaluator),
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

    return std::make_shared<TestConfig>(proto, runtime_, time_system_, random_, scope_,
                                        std::move(tls), controller_, evaluator_);
  }

  void setupFilter(std::shared_ptr<AdmissionControlFilterConfig> config) {
    filter_ = std::make_shared<AdmissionControlFilter>(config, "test_prefix.");
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void sampleGrpcRequest(std::string&& grpc_status) {
    Http::TestResponseHeaderMapImpl headers{{"content-type", "application/grpc"},
                                            {"grpc-status", grpc_status}};
    filter_->encodeHeaders(headers, true);
  }

  void sampleHttpRequest(std::string&& http_error_code) {
    Http::TestResponseHeaderMapImpl headers{{":status", http_error_code}};
    filter_->encodeHeaders(headers, true);
  }

protected:
  std::string stats_prefix_{""};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Stats::IsolatedStoreImpl scope_;
  Event::SimulatedTimeSystem time_system_;
  NiceMock<Runtime::MockRandomGenerator> random_;
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
  http_success_status:
  grpc_success_status:
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
  http_success_status:
  grpc_success_status:
)EOF";

  auto config = makeConfig(yaml);
  setupFilter(config);

  // "Disable" the filter via runtime.
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo.enabled", true)).WillRepeatedly(Return(false));

  // The filter is bypassed via runtime.
  EXPECT_CALL(controller_, requestTotalCount()).Times(0);
  EXPECT_CALL(controller_, requestSuccessCount()).Times(0);

  // We expect no rejections.
  Http::RequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Ensure the filter disregards healthcheck traffic.
TEST_F(AdmissionControlTest, DisregardHealthChecks) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(testing::ReturnRef(stream_info));
  EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

  // Fail lots of requests so that we would normally expect a ~100% rejection rate. It should pass
  // below since the request is a healthcheck.
  EXPECT_CALL(controller_, requestTotalCount()).Times(0);
  EXPECT_CALL(controller_, requestSuccessCount()).Times(0);

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
}

// Validate simple HTTP failure case.
TEST_F(AdmissionControlTest, HttpFailureBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(100));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(*evaluator_, isHttpSuccess(500)).WillRepeatedly(Return(false));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers;
  sampleHttpRequest("500");

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple HTTP success case.
TEST_F(AdmissionControlTest, HttpSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(100));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(100));
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

  // We expect rejection counter to increment upon failure.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(100));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(0));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(7)).WillRepeatedly(Return(false));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
  Http::TestResponseHeaderMapImpl response_headers;
  sampleGrpcRequest("7");

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 1, time_system_);
}

// Validate simple gRPC success case.
TEST_F(AdmissionControlTest, GrpcSuccessBehavior) {
  auto config = makeConfig(default_yaml_);
  setupFilter(config);

  // We expect rejection counter to NOT increment upon success.
  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);

  EXPECT_CALL(controller_, requestTotalCount()).WillRepeatedly(Return(100));
  EXPECT_CALL(controller_, requestSuccessCount()).WillRepeatedly(Return(100));
  EXPECT_CALL(*evaluator_, isGrpcSuccess(0)).WillRepeatedly(Return(true));

  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  sampleGrpcRequest("0");

  TestUtility::waitForCounterEq(scope_, "test_prefix.rq_rejected", 0, time_system_);
}

} // namespace
} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
