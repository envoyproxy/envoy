#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

#include "extensions/filters/http/fault/fault_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/fault/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Matcher;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {
namespace {

class FaultFilterTest : public testing::Test {
public:
  const std::string fixed_delay_and_abort_nodes_yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    http_status: 503
  downstream_nodes:
  - canary
  )EOF";

  const std::string fixed_delay_only_yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  )EOF";

  const std::string abort_only_yaml = R"EOF(
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    http_status: 429
  )EOF";

  const std::string fixed_delay_and_abort_yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    http_status: 503
  )EOF";

  const std::string header_abort_only_yaml = R"EOF(
  abort:
    header_abort: {}
    percentage:
      numerator: 100
  )EOF";

  const std::string fixed_delay_and_abort_match_headers_yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    http_status: 503
  headers:
  - name: X-Foo1
    exact_match: Bar
  - name: X-Foo2
  )EOF";

  const std::string delay_with_upstream_cluster_yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  upstream_cluster: www1
  )EOF";

  const std::string v2_empty_fault_config_yaml = "{}";

  void SetUpTest(const envoy::extensions::filters::http::fault::v3::HTTPFault fault) {
    config_ = std::make_shared<FaultFilterConfig>(fault, runtime_, "prefix.", stats_, time_system_);
    filter_ = std::make_unique<FaultFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
  }

  void SetUpTest(const std::string& yaml) { SetUpTest(convertYamlStrToProtoConfig(yaml)); }

  void expectDelayTimer(uint64_t duration_ms) {
    timer_ = new Event::MockTimer(&decoder_filter_callbacks_.dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(duration_ms), _));
    EXPECT_CALL(*timer_, disableTimer());
  }

  void TestPerFilterConfigFault(const Router::RouteSpecificFilterConfig* route_fault,
                                const Router::RouteSpecificFilterConfig* vhost_fault);

  NiceMock<Stats::MockIsolatedStatsStore> stats_;
  FaultFilterConfigSharedPtr config_;
  std::unique_ptr<FaultFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_filter_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Buffer::OwnedImpl data_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* timer_{};
  Event::SimulatedTimeSystem time_system_;
};

void faultFilterBadConfigHelper(const std::string& yaml) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, fault), EnvoyException);
}

TEST(FaultFilterBadConfigTest, EmptyDownstreamNodes) {
  const std::string yaml = R"EOF(
  abort:
    abort_percent:
      numerator: 80
      denominator: HUNDRED
    http_status: 503
  downstream_nodes: []

  )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST(FaultFilterBadConfigTest, MissingHTTPStatus) {
  const std::string yaml = R"EOF(
  abort:
    abort_percent:
      numerator: 100
      denominator: HUNDRED
  )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST(FaultFilterBadConfigTest, BadDelayType) {
  const std::string yaml = R"EOF(
  delay:
    type: foo
    percentage:
      numerator: 50
      denominator: HUNDRED
    fixed_delay: 5s
  )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST(FaultFilterBadConfigTest, BadDelayDuration) {
  const std::string yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 50
      denominator: HUNDRED
    fixed_delay: 0s
   )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST(FaultFilterBadConfigTest, MissingDelayDuration) {
  const std::string yaml = R"EOF(
  delay:
    type: fixed
    percentage:
      numerator: 50
      denominator: HUNDRED
   )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST_F(FaultFilterTest, AbortWithHttpStatus) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort()->mutable_percentage()->set_numerator(100);
  fault.mutable_abort()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_abort()->set_http_status(429);
  SetUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 429))
      .WillOnce(Return(429));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "429"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details_);
}

TEST_F(FaultFilterTest, HeaderAbortWithHttpStatus) {
  SetUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-request", "429");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 429))
      .WillOnce(Return(429));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "429"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details_);
}

TEST_F(FaultFilterTest, AbortWithGrpcStatus) {
  decoder_filter_callbacks_.is_grpc_request_ = true;

  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort()->mutable_percentage()->set_numerator(100);
  fault.mutable_abort()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_abort()->set_grpc_status(5);
  SetUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.grpc_status", 5))
      .WillOnce(Return(5));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"},
                                                   {"grpc-status", "5"},
                                                   {"grpc-message", "fault filter abort"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details_);
}

TEST_F(FaultFilterTest, HeaderAbortWithGrpcStatus) {
  decoder_filter_callbacks_.is_grpc_request_ = true;
  SetUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-grpc-request", "5");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.grpc_status", 5))
      .WillOnce(Return(5));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/grpc"},
                                                   {"grpc-status", "5"},
                                                   {"grpc-message", "fault filter abort"}};

  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details_);
}

TEST_F(FaultFilterTest, HeaderAbortWithHttpAndGrpcStatus) {
  SetUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-request", "429");
  request_headers_.addCopy("x-envoy-fault-abort-grpc-request", "5");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 429))
      .WillOnce(Return(429));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.grpc_status", 5)).Times(0);

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "429"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details_);
}

TEST_F(FaultFilterTest, FixedDelayZeroDuration) {
  SetUpTest(fixed_delay_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  // Return 0ms delay
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(0));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setResponseFlag(_)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Expect filter to continue execution when delay is 0ms
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, Overflow) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_max_active_faults()->set_value(0);
  SetUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.max_active_faults", 0))
      .WillOnce(Return(0));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1UL, config_->stats().faults_overflow_.value());
}

TEST_F(FaultFilterTest, FixedDelayDeprecatedPercentAndNonZeroDuration) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_delay()->mutable_percentage()->set_numerator(50);
  fault.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_delay()->mutable_fixed_delay()->set_seconds(5);
  SetUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayDeprecatedPercentAndNonZeroDuration");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  timer_->invokeCallback();
  filter_->onDestroy();

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterTest, DelayForDownstreamCluster) {
  SetUpTest(fixed_delay_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  request_headers_.addCopy("x-envoy-downstream-service-cluster", "cluster");

  // Delay related calls.
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.cluster.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(125UL));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.delay.fixed_duration_ms", 125UL))
      .WillOnce(Return(500UL));
  expectDelayTimer(500UL);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case, no aborts.
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.abort.http_status", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));

  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(2);
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.delays_injected").value());
  EXPECT_EQ(0UL, stats_.counter("prefix.fault.cluster.aborts_injected").value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortDownstream) {
  SetUpTest(fixed_delay_and_abort_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  request_headers_.addCopy("x-envoy-downstream-service-cluster", "cluster");

  // Delay related calls.
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.cluster.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(125UL));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.delay.fixed_duration_ms", 125UL))
      .WillOnce(Return(500UL));
  expectDelayTimer(500UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.cluster.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.abort.http_status", 503))
      .WillOnce(Return(500));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "500"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  filter_->onDestroy();

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.delays_injected").value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.aborts_injected").value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbort) {
  SetUpTest(fixed_delay_and_abort_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayAndAbort");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortDownstreamNodes) {
  SetUpTest(fixed_delay_and_abort_nodes_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls.
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  request_headers_.addCopy("x-envoy-downstream-service-node", "canary");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls.
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, NoDownstreamMatch) {
  SetUpTest(fixed_delay_and_abort_nodes_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(FaultFilterTest, FixedDelayAndAbortHeaderMatchSuccess) {
  SetUpTest(fixed_delay_and_abort_match_headers_yaml);
  request_headers_.addCopy("x-foo1", "Bar");
  request_headers_.addCopy("x-foo2", "RandomValue");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayAndAbortHeaderMatchSuccess");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortHeaderMatchFail) {
  SetUpTest(fixed_delay_and_abort_match_headers_yaml);
  request_headers_.addCopy("x-foo1", "Bar");
  request_headers_.addCopy("x-foo3", "Baz");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setResponseFlag(_)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Expect filter to continue execution when headers don't match
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, TimerResetAfterStreamReset) {
  SetUpTest(fixed_delay_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Prep up with a 5s delay
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayWithStreamReset");
  timer_ = new Event::MockTimer(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000UL), _));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());

  // delay timer should have been fired by now. If caller resets the stream while we are waiting
  // on the delay timer, check if timers are cancelled
  EXPECT_CALL(*timer_, disableTimer());

  // The timer callback should never be called.
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, true));

  filter_->onDestroy();
}

TEST_F(FaultFilterTest, FaultWithTargetClusterMatchSuccess) {
  SetUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("www1");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FaultWithTargetClusterMatchSuccess");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FaultWithTargetClusterMatchFail) {
  SetUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("mismatch");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));
  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setResponseFlag(_)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FaultWithTargetClusterNullRoute) {
  SetUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("www1");

  EXPECT_CALL(*decoder_filter_callbacks_.route_, routeEntry()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setResponseFlag(_)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

void FaultFilterTest::TestPerFilterConfigFault(
    const Router::RouteSpecificFilterConfig* route_fault,
    const Router::RouteSpecificFilterConfig* vhost_fault) {

  ON_CALL(decoder_filter_callbacks_.route_->route_entry_,
          perFilterConfig(Extensions::HttpFilters::HttpFilterNames::get().Fault))
      .WillByDefault(Return(route_fault));
  ON_CALL(decoder_filter_callbacks_.route_->route_entry_.virtual_host_,
          perFilterConfig(Extensions::HttpFilters::HttpFilterNames::get().Fault))
      .WillByDefault(Return(vhost_fault));

  const std::string upstream_cluster("www1");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("PerFilterConfigFault");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::ResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, RouteFaultOverridesListenerFault) {

  Fault::FaultSettings abort_fault(convertYamlStrToProtoConfig(abort_only_yaml));
  Fault::FaultSettings delay_fault(convertYamlStrToProtoConfig(delay_with_upstream_cluster_yaml));

  // route-level fault overrides listener-level fault
  {
    SetUpTest(v2_empty_fault_config_yaml); // This is a valid listener level fault
    TestPerFilterConfigFault(&delay_fault, nullptr);
  }

  // virtual-host-level fault overrides listener-level fault
  {
    config_->stats().aborts_injected_.reset();
    config_->stats().delays_injected_.reset();
    SetUpTest(v2_empty_fault_config_yaml);
    TestPerFilterConfigFault(nullptr, &delay_fault);
  }

  // route-level fault overrides virtual-host-level fault
  {
    config_->stats().aborts_injected_.reset();
    config_->stats().delays_injected_.reset();
    SetUpTest(v2_empty_fault_config_yaml);
    TestPerFilterConfigFault(&delay_fault, &abort_fault);
  }
}

class FaultFilterRateLimitTest : public FaultFilterTest {
public:
  void setupRateLimitTest(bool enable_runtime) {
    envoy::extensions::filters::http::fault::v3::HTTPFault fault;
    fault.mutable_response_rate_limit()->mutable_fixed_limit()->set_limit_kbps(1);
    fault.mutable_response_rate_limit()->mutable_percentage()->set_numerator(100);
    SetUpTest(fault);

    EXPECT_CALL(runtime_.snapshot_,
                featureEnabled("fault.http.rate_limit.response_percent",
                               Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
        .WillOnce(Return(enable_runtime));
  }
};

TEST_F(FaultFilterRateLimitTest, ResponseRateLimitDisabled) {
  setupRateLimitTest(false);
  Buffer::OwnedImpl data;

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
}

// Make sure we destroy the rate limiter if we are reset.
TEST_F(FaultFilterRateLimitTest, DestroyWithResponseRateLimitEnabled) {
  setupRateLimitTest(true);

  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  // The timer is consumed but not used by this test.
  new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1UL, config_->stats().response_rl_injected_.value());
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterRateLimitTest, ResponseRateLimitEnabled) {
  setupRateLimitTest(true);

  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1UL, config_->stats().response_rl_injected_.value());
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual("hello"), false));
  token_timer->invokeCallback();

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Send 1152 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1152, 'a'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'a')), false));
  token_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(64, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time. No time enable because there is nothing buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'b')), false));
  token_timer->invokeCallback();

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in one shot with end_stream true which should go through and end the stream.
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data4(std::string(1024, 'c'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data4, true));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(1024, 'c')), true));
  token_timer->invokeCallback();

  filter_->onDestroy();
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

class FaultFilterSettingsTest : public FaultFilterTest {};

TEST_F(FaultFilterSettingsTest, CheckDefaultRuntimeKeys) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;

  Fault::FaultSettings settings(fault);

  EXPECT_EQ("fault.http.delay.fixed_delay_percent", settings.delayPercentRuntime());
  EXPECT_EQ("fault.http.abort.abort_percent", settings.abortPercentRuntime());
  EXPECT_EQ("fault.http.delay.fixed_duration_ms", settings.delayDurationRuntime());
  EXPECT_EQ("fault.http.abort.http_status", settings.abortHttpStatusRuntime());
  EXPECT_EQ("fault.http.abort.grpc_status", settings.abortGrpcStatusRuntime());
  EXPECT_EQ("fault.http.max_active_faults", settings.maxActiveFaultsRuntime());
  EXPECT_EQ("fault.http.rate_limit.response_percent", settings.responseRateLimitPercentRuntime());
}

TEST_F(FaultFilterSettingsTest, CheckOverrideRuntimeKeys) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.set_abort_percent_runtime(std::string("fault.abort_percent_runtime"));
  fault.set_delay_percent_runtime(std::string("fault.delay_percent_runtime"));
  fault.set_abort_http_status_runtime(std::string("fault.abort_http_status_runtime"));
  fault.set_abort_grpc_status_runtime(std::string("fault.abort_grpc_status_runtime"));
  fault.set_delay_duration_runtime(std::string("fault.delay_duration_runtime"));
  fault.set_max_active_faults_runtime(std::string("fault.max_active_faults_runtime"));
  fault.set_response_rate_limit_percent_runtime(
      std::string("fault.response_rate_limit_percent_runtime"));

  Fault::FaultSettings settings(fault);

  EXPECT_EQ("fault.delay_percent_runtime", settings.delayPercentRuntime());
  EXPECT_EQ("fault.abort_percent_runtime", settings.abortPercentRuntime());
  EXPECT_EQ("fault.delay_duration_runtime", settings.delayDurationRuntime());
  EXPECT_EQ("fault.abort_http_status_runtime", settings.abortHttpStatusRuntime());
  EXPECT_EQ("fault.abort_grpc_status_runtime", settings.abortGrpcStatusRuntime());
  EXPECT_EQ("fault.max_active_faults_runtime", settings.maxActiveFaultsRuntime());
  EXPECT_EQ("fault.response_rate_limit_percent_runtime",
            settings.responseRateLimitPercentRuntime());
}

} // namespace
} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
