#include <chrono>

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"

#include "source/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/controller.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {
namespace {

using Controller::RequestForwardingAction;

class MockConcurrencyController : public Controller::ConcurrencyController {
public:
  MOCK_METHOD(RequestForwardingAction, forwardingDecision, ());
  MOCK_METHOD(void, cancelLatencySample, ());
  MOCK_METHOD(void, recordLatencySample, (MonotonicTime));

  uint32_t concurrencyLimit() const override { return 0; }
};

class AdaptiveConcurrencyFilterTest : public testing::Test {
public:
  AdaptiveConcurrencyFilterTest() = default;

  void SetUp() override {
    const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency config;
    auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
        config, runtime_, "testprefix.", stats_, time_system_);

    filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void TearDown() override { filter_.reset(); }

  envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency
  makeConfig(const std::string& yaml_config) {
    envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency proto;
    TestUtility::loadFromYamlAndValidate(yaml_config, proto);
    return proto;
  }

  Event::SimulatedTimeSystem time_system_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::Scope& stats_{*stats_store_.rootScope()};
  NiceMock<Runtime::MockLoader> runtime_;
  std::shared_ptr<MockConcurrencyController> controller_{new MockConcurrencyController()};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<AdaptiveConcurrencyFilter> filter_;
};

TEST_F(AdaptiveConcurrencyFilterTest, TestEnableOverriddenFromRuntime) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      seconds: 30
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  auto config = makeConfig(yaml_config);

  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);
  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  // The filter should behave as normal here.

  Http::TestRequestHeaderMapImpl request_headers;

  // The filter will be disabled when the flag is overridden. Note there is no expected call to
  // forwardingDecision() or recordLatencySample().

  EXPECT_CALL(runtime_.snapshot_, getBoolean("adaptive_concurrency.enabled", true))
      .WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->encodeComplete();
}

TEST_F(AdaptiveConcurrencyFilterTest, TestNanosValidationFail) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      nanos: 8
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  EXPECT_THROW(auto config = makeConfig(yaml_config), ProtoValidationException);
}

TEST_F(AdaptiveConcurrencyFilterTest, TestNanosValidationPass) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      nanos: 1000000
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  auto config = makeConfig(yaml_config);

  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);
  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  // The filter should behave as normal here.

  Http::TestRequestHeaderMapImpl request_headers;

  // The filter will be disabled when the flag is overridden. Note there is no expected call to
  // forwardingDecision() or recordLatencySample().

  EXPECT_CALL(runtime_.snapshot_, getBoolean("adaptive_concurrency.enabled", true))
      .WillOnce(Return(false));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->encodeComplete();
}

TEST_F(AdaptiveConcurrencyFilterTest, TestEnableConfiguredInProto) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      seconds: 30
    request_count: 50
enabled:
  default_value: false
  runtime_key: "adaptive_concurrency.enabled"
)EOF";

  auto config = makeConfig(yaml_config);

  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);
  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  // We expect no calls to the concurrency controller.

  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  filter_->encodeComplete();
}

TEST_F(AdaptiveConcurrencyFilterTest, DecodeHeadersTestForwarding) {
  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  EXPECT_CALL(*controller_, recordLatencySample(_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  Buffer::OwnedImpl request_body;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(request_body, false));

  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

TEST_F(AdaptiveConcurrencyFilterTest, DecodeHeadersTestBlock) {
  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(*controller_, forwardingDecision()).WillOnce(Return(RequestForwardingAction::Block));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AdaptiveConcurrencyFilterTest, RecordSampleInDestructor) {
  // Verify that the request latency is always sampled even if encodeComplete() is never called.
  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  Http::TestRequestHeaderMapImpl request_headers;
  filter_->decodeHeaders(request_headers, true);

  EXPECT_CALL(*controller_, recordLatencySample(_));
  filter_.reset();
}

TEST_F(AdaptiveConcurrencyFilterTest, RecordSampleOmission) {
  // Verify that the request latency is not sampled if forwardingDecision blocks the request.
  EXPECT_CALL(*controller_, forwardingDecision()).WillOnce(Return(RequestForwardingAction::Block));
  Http::TestRequestHeaderMapImpl request_headers;
  filter_->decodeHeaders(request_headers, true);

  filter_.reset();
}

TEST_F(AdaptiveConcurrencyFilterTest, OnDestroyCleanupResetTest) {
  // Get the filter to record the request start time via decode.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  EXPECT_CALL(*controller_, cancelLatencySample());

  // Encode step is not performed prior to destruction.
  filter_->onDestroy();
}

TEST_F(AdaptiveConcurrencyFilterTest, OnDestroyCleanupTest) {
  // Get the filter to record the request start time via decode.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  const auto rq_rcv_time = time_system_.monotonicTime();
  time_system_.advanceTimeWait(std::chrono::nanoseconds(42));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_CALL(*controller_, recordLatencySample(rq_rcv_time));
  filter_->encodeComplete();

  filter_->onDestroy();
}

TEST_F(AdaptiveConcurrencyFilterTest, EncodeHeadersValidTestWithBody) {
  auto mt = time_system_.monotonicTime();
  time_system_.setMonotonicTime(mt + std::chrono::nanoseconds(123));

  // Get the filter to record the request start time via decode.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));

  const auto rq_rcv_time = time_system_.monotonicTime();
  mt = time_system_.monotonicTime();
  time_system_.setMonotonicTime(mt + std::chrono::nanoseconds(42));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
  EXPECT_CALL(*controller_, recordLatencySample(rq_rcv_time));
  filter_->encodeComplete();
}

TEST_F(AdaptiveConcurrencyFilterTest, EncodeHeadersValidTest) {
  auto mt = time_system_.monotonicTime();
  time_system_.setMonotonicTime(mt + std::chrono::nanoseconds(123));

  // Get the filter to record the request start time via decode.
  Http::TestRequestHeaderMapImpl request_headers;
  EXPECT_CALL(*controller_, forwardingDecision())
      .WillOnce(Return(RequestForwardingAction::Forward));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  const auto rq_rcv_time = time_system_.monotonicTime();
  mt = time_system_.monotonicTime();
  time_system_.setMonotonicTime(mt + std::chrono::nanoseconds(42));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_CALL(*controller_, recordLatencySample(rq_rcv_time));
  filter_->encodeComplete();
}

TEST_F(AdaptiveConcurrencyFilterTest, DisregardHealthChecks) {
  StreamInfo::MockStreamInfo stream_info;
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  EXPECT_CALL(stream_info, healthCheck()).WillOnce(Return(true));

  Http::TestRequestHeaderMapImpl request_headers;

  // We do not expect a call to forwardingDecision() during decode.

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));

  // We do not expect a call to recordLatencySample() as well.

  filter_->encodeComplete();
}

/**
 * Tests that if configured a custom status code is returned
 * from the adaptive concurrency filter
 */
TEST_F(AdaptiveConcurrencyFilterTest, DecodeHeadersTestBlockWithCustomStatus) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      nanos: 1000000
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
concurrency_limit_exceeded_status:
  code: 429
)EOF";

  auto config = makeConfig(yaml_config);

  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);
  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(*controller_, forwardingDecision()).WillOnce(Return(RequestForwardingAction::Block));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::TooManyRequests, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

/**
 * Tests that if an invalid (<400) status code is configured then the
 * Adaptive Concurrency Filter falls back to 503
 */
TEST_F(AdaptiveConcurrencyFilterTest,
       DecodeHeadersTestBlockWithServiceUnavailableForInvalidStatus) {
  std::string yaml_config =
      R"EOF(
gradient_controller_config:
  sample_aggregate_percentile:
    value: 50
  concurrency_limit_params:
    concurrency_update_interval:
      nanos: 100000000 # 100ms
  min_rtt_calc_params:
    interval:
      nanos: 1000000
    request_count: 50
enabled:
  default_value: true
  runtime_key: "adaptive_concurrency.enabled"
concurrency_limit_exceeded_status:
  code: 200
)EOF";

  auto config = makeConfig(yaml_config);

  auto config_ptr = std::make_shared<AdaptiveConcurrencyFilterConfig>(
      config, runtime_, "testprefix.", stats_, time_system_);
  filter_ = std::make_unique<AdaptiveConcurrencyFilter>(config_ptr, controller_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  Http::TestRequestHeaderMapImpl request_headers;

  EXPECT_CALL(*controller_, forwardingDecision()).WillOnce(Return(RequestForwardingAction::Block));
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::ServiceUnavailable, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

} // namespace
} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
