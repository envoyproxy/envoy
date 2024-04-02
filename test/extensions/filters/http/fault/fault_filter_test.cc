#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/extensions/filters/http/fault/fault_filter.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/fault/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
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
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  filter_metadata:
    hello: "world"
  )EOF";

  const std::string fixed_delay_only_disable_stats_yaml = R"EOF(
  delay:
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  disable_downstream_cluster_stats: true
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
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  abort:
    percentage:
      numerator: 100
      denominator: HUNDRED
    http_status: 503
  filter_metadata:
    hello: "world"
  )EOF";

  const std::string header_abort_only_yaml = R"EOF(
  filter_metadata:
    hello: "world"
  abort:
    header_abort: {}
    percentage:
      numerator: 100
  )EOF";

  const std::string fixed_delay_and_abort_match_headers_yaml = R"EOF(
  delay:
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
    string_match:
      exact: Bar
  - name: X-Foo2
  )EOF";

  const std::string delay_with_upstream_cluster_yaml = R"EOF(
  delay:
    percentage:
      numerator: 100
      denominator: HUNDRED
    fixed_delay: 5s
  upstream_cluster: www1
  )EOF";

  const std::string v2_empty_fault_config_yaml = "{}";

  void setUpTest(const envoy::extensions::filters::http::fault::v3::HTTPFault fault) {
    ON_CALL(context_, timeSource()).WillByDefault(ReturnRef(time_system_));
    config_ = std::make_shared<FaultFilterConfig>(fault, "prefix.", *stats_.rootScope(), context_);
    filter_ = std::make_unique<FaultFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
    ON_CALL(decoder_filter_callbacks_, filterConfigName)
        .WillByDefault(Return("envoy.filters.http.fault"));
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  }

  void setUpTest(const std::string& yaml) { setUpTest(convertYamlStrToProtoConfig(yaml)); }

  void expectDelayTimer(uint64_t duration_ms) {
    timer_ = new Event::MockTimer(&decoder_filter_callbacks_.dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(duration_ms), _));
    EXPECT_CALL(*timer_, disableTimer());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
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
  NiceMock<Runtime::MockLoader>& runtime_{context_.runtime_loader_};
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
    percentage:
      numerator: 50
      denominator: HUNDRED
   )EOF";

  faultFilterBadConfigHelper(yaml);
}

TEST_F(FaultFilterTest, AbortWithHttpStatus) {
  setUpTest(abort_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 429))
      .WillOnce(Return(429));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "429"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

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
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details());
}

TEST_F(FaultFilterTest, HeaderAbortWithHttpStatus) {
  setUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-request", "429");

  envoy::config::core::v3::Metadata dynamic_metadata;
  envoy::config::core::v3::Metadata expected_metadata;
  auto& filter_metadata = *expected_metadata.mutable_filter_metadata();
  (*filter_metadata["envoy.filters.http.fault"].mutable_fields())["hello"].set_string_value(
      "world");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 429))
      .WillOnce(Return(429));
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillOnce(ReturnRef(dynamic_metadata));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "429"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

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
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details());
  EXPECT_THAT(dynamic_metadata, ProtoEq(expected_metadata));
}

TEST_F(FaultFilterTest, AbortWithGrpcStatus) {
  decoder_filter_callbacks_.is_grpc_request_ = true;

  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_abort()->mutable_percentage()->set_numerator(100);
  fault.mutable_abort()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_abort()->set_grpc_status(5);
  setUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
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
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

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
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details());
}

TEST_F(FaultFilterTest, HeaderAbortWithGrpcStatus) {
  decoder_filter_callbacks_.is_grpc_request_ = true;
  setUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-grpc-request", "5");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
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
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

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
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details());
}

TEST_F(FaultFilterTest, HeaderAbortWithHttpAndGrpcStatus) {
  setUpTest(header_abort_only_yaml);

  request_headers_.addCopy("x-envoy-fault-abort-request", "429");
  request_headers_.addCopy("x-envoy-fault-abort-grpc-request", "5");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(0);

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
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
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

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
  EXPECT_EQ("fault_filter_abort", decoder_filter_callbacks_.details());
}

TEST_F(FaultFilterTest, FixedDelayZeroDuration) {
  setUpTest(fixed_delay_only_yaml);

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  // Return 0ms delay
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(0));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, setResponseFlag(_)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata()).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  // Expect filter to continue execution when delay is 0ms
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(0UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, Overflow) {
  setUpTest(fixed_delay_only_yaml);

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  // Return 1ms delay
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(1));

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(0));
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ(1UL, config_->stats().faults_overflow_.value());
}

// Verifies that we don't increment the active_faults gauge when not applying a fault.
TEST_F(FaultFilterTest, Passthrough) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  setUpTest(fault);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterTest, FixedDelayDeprecatedPercentAndNonZeroDuration) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_delay()->mutable_percentage()->set_numerator(50);
  fault.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_delay()->mutable_fixed_delay()->set_seconds(5);
  setUpTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(50))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayDeprecatedPercentAndNonZeroDuration");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
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

// Verifies that 2 consecutive delay faults be allowed with the max number of faults set to 1.
TEST_F(FaultFilterTest, ConsecutiveDelayFaults) {
  setUpTest(fixed_delay_only_yaml);

  envoy::config::core::v3::Metadata dynamic_metadata;
  envoy::config::core::v3::Metadata expected_metadata;
  auto& filter_metadata = *expected_metadata.mutable_filter_metadata();
  (*filter_metadata["envoy.filters.http.fault"].mutable_fields())["hello"].set_string_value(
      "world");

  // Set the max number of faults to 1.
  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(1))
      .WillOnce(Return(1));

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("ConsecutiveDelayFaults");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected))
      .Times(2);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillOnce(ReturnRef(dynamic_metadata))
      .WillOnce(ReturnRef(dynamic_metadata));

  // Start request 1 with a fault delay.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case, no aborts.
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.abort.http_status", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(2);

  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_));

  // Finish request 1 delay but not request 1.
  timer_->invokeCallback();

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  // Should stop counting as active fault after delay elapsed.
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_THAT(dynamic_metadata, ProtoEq(expected_metadata));

  // Prep up request 2, with setups and expectations same as request 1.
  setUpTest(fixed_delay_only_yaml);
  expectDelayTimer(5000UL);

  // Start request 2.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // No overflow should happen as we stop counting active after the first elapsed.
  EXPECT_EQ(0UL, config_->stats().faults_overflow_.value());
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_));

  // Have the fault delay of request 2 kick in, which should be delayed with success.
  timer_->invokeCallback();
  EXPECT_THAT(dynamic_metadata, ProtoEq(expected_metadata));
  filter_->onDestroy();

  EXPECT_EQ(2UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterTest, DelayForDownstreamCluster) {
  setUpTest(fixed_delay_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  request_headers_.addCopy("x-envoy-downstream-service-cluster", "cluster");

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.cluster.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(125UL));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.delay.fixed_duration_ms", 125UL))
      .WillOnce(Return(500UL));
  expectDelayTimer(500UL);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case, no aborts.
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.abort.http_status", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));

  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_));
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.delays_injected").value());
  EXPECT_EQ(0UL, stats_.counter("prefix.fault.cluster.aborts_injected").value());
}

TEST_F(FaultFilterTest, DelayForDownstreamClusterDisableTracing) {
  setUpTest(fixed_delay_only_disable_stats_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  request_headers_.addCopy("x-envoy-downstream-service-cluster", "cluster");

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.cluster.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(125UL));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.delay.fixed_duration_ms", 125UL))
      .WillOnce(Return(500UL));
  expectDelayTimer(500UL);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case, no aborts.
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.abort.http_status", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));

  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_));
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0UL, stats_.counter("prefix.fault.cluster.delays_injected").value());
  EXPECT_EQ(0UL, stats_.counter("prefix.fault.cluster.aborts_injected").value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortDownstream) {
  setUpTest(fixed_delay_and_abort_yaml);

  envoy::config::core::v3::Metadata dynamic_metadata;
  ON_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata())
      .WillByDefault(ReturnRef(dynamic_metadata));

  envoy::config::core::v3::Metadata expected_metadata;
  auto& filter_metadata = *expected_metadata.mutable_filter_metadata();
  (*filter_metadata["envoy.filters.http.fault"].mutable_fields())["hello"].set_string_value(
      "world");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  request_headers_.addCopy("x-envoy-downstream-service-cluster", "cluster");

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.cluster.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(125UL));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.cluster.delay.fixed_duration_ms", 125UL))
      .WillOnce(Return(500UL));
  expectDelayTimer(500UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.cluster.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
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
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_THAT(dynamic_metadata, ProtoEq(expected_metadata));
  filter_->onDestroy();

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.delays_injected").value());
  EXPECT_EQ(1UL, stats_.counter("prefix.fault.cluster.aborts_injected").value());
  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbort) {
  setUpTest(fixed_delay_and_abort_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayAndAbort");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortDownstreamNodes) {
  setUpTest(fixed_delay_and_abort_nodes_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  request_headers_.addCopy("x-envoy-downstream-service-node", "canary");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, NoDownstreamMatch) {
  setUpTest(fixed_delay_and_abort_nodes_yaml);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(FaultFilterTest, FixedDelayAndAbortHeaderMatchSuccess) {
  setUpTest(fixed_delay_and_abort_match_headers_yaml);
  request_headers_.addCopy("x-foo1", "Bar");
  request_headers_.addCopy("x-foo2", "RandomValue");

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayAndAbortHeaderMatchSuccess");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Abort related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.abort.abort_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", 503))
      .WillOnce(Return(503));

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "503"}, {"content-length", "18"}, {"content-type", "text/plain"}};
  EXPECT_CALL(decoder_filter_callbacks_,
              encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(decoder_filter_callbacks_, encodeData(_, true));
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);

  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FixedDelayAndAbortHeaderMatchFail) {
  setUpTest(fixed_delay_and_abort_match_headers_yaml);
  request_headers_.addCopy("x-foo1", "Bar");
  request_headers_.addCopy("x-foo3", "Baz");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
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
  setUpTest(fixed_delay_only_yaml);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Prep up with a 5s delay
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FixedDelayWithStreamReset");
  timer_ = new Event::MockTimer(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(5000UL), _));

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

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
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding()).Times(0);
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, true));

  filter_->onDestroy();
}

TEST_F(FaultFilterTest, FaultWithTargetClusterMatchSuccess) {
  setUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("www1");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("FaultWithTargetClusterMatchSuccess");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  // Delay only case
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterTest, FaultWithTargetClusterMatchFail) {
  setUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("mismatch");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
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
  setUpTest(delay_with_upstream_cluster_yaml);
  const std::string upstream_cluster("www1");

  EXPECT_CALL(*decoder_filter_callbacks_.route_, routeEntry()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.delay.fixed_delay_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .Times(0);
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", _)).Times(0);
  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled("fault.http.abort.abort_percent",
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
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

TEST_F(FaultFilterTest, RouteFaultOverridesListenerFault) {
  setUpTest(v2_empty_fault_config_yaml);
  Fault::FaultSettings delay_fault(convertYamlStrToProtoConfig(delay_with_upstream_cluster_yaml),
                                   context_);

  ON_CALL(*decoder_filter_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillByDefault(Return(&delay_fault));

  const std::string upstream_cluster("www1");

  EXPECT_CALL(decoder_filter_callbacks_.route_->route_entry_, clusterName())
      .WillOnce(ReturnRef(upstream_cluster));

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("PerFilterConfigFault");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));

  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());
  timer_->invokeCallback();

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));

  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

class FaultFilterRateLimitTest : public FaultFilterTest {
public:
  void setupRateLimitTest(bool enable_runtime) {
    envoy::extensions::filters::http::fault::v3::HTTPFault fault;
    fault.mutable_response_rate_limit()->mutable_fixed_limit()->set_limit_kbps(1);
    fault.mutable_response_rate_limit()->mutable_percentage()->set_numerator(100);
    setUpTest(fault);

    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled("fault.http.rate_limit.response_percent",
                       testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
        .WillOnce(Return(enable_runtime));
  }

  // Enable rate limit test with customized fault config input.
  void setupRateLimitTest(envoy::extensions::filters::http::fault::v3::HTTPFault fault) {
    fault.mutable_response_rate_limit()->mutable_fixed_limit()->set_limit_kbps(1);
    fault.mutable_response_rate_limit()->mutable_percentage()->set_numerator(100);
    setUpTest(fault);

    EXPECT_CALL(
        runtime_.snapshot_,
        featureEnabled("fault.http.rate_limit.response_percent",
                       testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
        .WillOnce(Return(true));
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

// Make sure the rate limiter doesn't free up after the delay elapsed.
// Regression test for https://github.com/envoyproxy/envoy/pull/14762.
TEST_F(FaultFilterRateLimitTest, DelayWithResponseRateLimitEnabled) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  fault.mutable_delay()->mutable_percentage()->set_numerator(100);
  fault.mutable_delay()->mutable_percentage()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  fault.mutable_delay()->mutable_fixed_delay()->set_seconds(5);
  setupRateLimitTest(fault);

  EXPECT_CALL(runtime_.snapshot_,
              getInteger("fault.http.max_active_faults", std::numeric_limits<uint64_t>::max()))
      .WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  // Delay related calls.
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("fault.http.delay.fixed_delay_percent",
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(Percent(100))))
      .WillOnce(Return(true));

  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.delay.fixed_duration_ms", 5000))
      .WillOnce(Return(5000UL));

  SCOPED_TRACE("DelayWithResponseRateLimitEnabled");
  expectDelayTimer(5000UL);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::DelayInjected));

  // Rate limiter related calls.
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  // The timer is consumed but not used by this test.
  new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, true));

  // Delay with rate limiter enabled case.
  EXPECT_CALL(runtime_.snapshot_, getInteger("fault.http.abort.http_status", _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_, encodeHeaders_(_, _)).Times(0);
  EXPECT_CALL(decoder_filter_callbacks_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected))
      .Times(0);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data_, false));
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());
  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().response_rl_injected_.value());
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  EXPECT_CALL(decoder_filter_callbacks_, continueDecoding());

  timer_->invokeCallback();

  // Make sure the rate limiter doesn't free up after the delay elapsed.
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  filter_->onDestroy();

  EXPECT_EQ(0UL, config_->stats().active_faults_.value());
  EXPECT_EQ(1UL, config_->stats().delays_injected_.value());
  EXPECT_EQ(1UL, config_->stats().response_rl_injected_.value());
  EXPECT_EQ(0UL, config_->stats().aborts_injected_.value());
}

TEST_F(FaultFilterRateLimitTest, ResponseRateLimitEnabled) {
  // Set metadata in fault. Ensure that it does not get reflected in stream info.
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  (*fault.mutable_filter_metadata()->mutable_fields())["hello"].set_string_value("world");

  setupRateLimitTest(fault);

  EXPECT_CALL(decoder_filter_callbacks_.stream_info_, dynamicMetadata()).Times(0);
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1UL, config_->stats().response_rl_injected_.value());
  EXPECT_EQ(1UL, config_->stats().active_faults_.value());

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
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

  // Send 1126 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1126, 'a'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(51, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time. No time enable because there is nothing buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'b')), false));
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

  Fault::FaultSettings settings(fault, context_);

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

  Fault::FaultSettings settings(fault, context_);

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
