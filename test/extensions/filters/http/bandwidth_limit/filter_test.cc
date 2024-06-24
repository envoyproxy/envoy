#include "envoy/extensions/filters/http/bandwidth_limit/v3/bandwidth_limit.pb.h"

#include "source/extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include "test/mocks/http/mocks.h"

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
namespace BandwidthLimitFilter {

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  void setup(const std::string& yaml) {
    envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit config;
    TestUtility::loadFromYaml(yaml, config);
    config_ =
        std::make_shared<FilterConfig>(config, *stats_.rootScope(), runtime_, time_system_, true);
    filter_ = std::make_shared<BandwidthLimiter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);

    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(encoder_filter_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(encoder_filter_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  uint64_t findGauge(const std::string& name) {
    const auto gauge = TestUtility::findGauge(stats_, name);
    return gauge != nullptr ? gauge->value() : 0;
  }

  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_filter_callbacks_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<BandwidthLimiter> filter_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Buffer::OwnedImpl data_;
  Event::SimulatedTimeSystem time_system_;
  Http::TestResponseTrailerMapImpl trailers_;
};

TEST_F(FilterTest, Disabled) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: false
    runtime_key: foo_key
  enable_mode: DISABLED
  limit_kbps: 10
  fill_interval: 1s
  enable_response_trailers: true
  )";
  setup(fmt::format(config_yaml, "1"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(0U, findCounter("test.http_bandwidth_limit.request_enabled"));
  EXPECT_EQ(0U, findCounter("test.http_bandwidth_limit.request_enforced"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(0U, findCounter("test.http_bandwidth_limit.response_enabled"));
  EXPECT_EQ(false, response_trailers_.has("bandwidth-request-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("bandwidth-response-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("bandwidth-request-filter-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("bandwidth-response-filter-delay-ms"));
}

TEST_F(FilterTest, LimitOnDecode) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: true
    runtime_key: foo_key
  enable_mode: REQUEST
  limit_kbps: 1
  enable_response_trailers: true
  response_trailer_prefix: test
  )";
  setup(fmt::format(config_yaml, "1"));

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1U, findCounter("test.http_bandwidth_limit.request_enabled"));
  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(50UL, config_->fillInterval().count());

  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(5, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(5, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  token_timer->invokeCallback();
  EXPECT_EQ(0, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(5, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(5, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Send 1126 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1126, 'a'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data2, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(1126, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(1, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(1029, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));
  EXPECT_EQ(1126, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(2, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(1080, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));
  EXPECT_EQ(1126, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(51, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data3, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(1182, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));

  // Fire timer, also advance time. No timer enable because there is nothing
  // buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'b')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(1182, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in one shot with end_stream true which should go through and
  // end the stream.
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data4(std::string(1024, 'c'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data4, true));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.request_incoming_size"));
  EXPECT_EQ(2206, findCounter("test.http_bandwidth_limit.request_incoming_total_size"));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'c')), true));
  token_timer->invokeCallback();
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.request_enforced"));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.request_allowed_size"));
  EXPECT_EQ(2206, findCounter("test.http_bandwidth_limit.request_allowed_total_size"));
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-response-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-filter-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-response-filter-delay-ms"));

  filter_->onDestroy();
}

TEST_F(FilterTest, LimitOnEncode) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: true
    runtime_key: foo_key
  enable_mode: RESPONSE
  limit_kbps: 1
  enable_response_trailers: true
  response_trailer_prefix: test
  )";
  setup(fmt::format(config_yaml, "1"));

  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  ON_CALL(encoder_filter_callbacks_, addEncodedTrailers()).WillByDefault(ReturnRef(trailers_));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(50UL, config_->fillInterval().count());

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(1U, findCounter("test.http_bandwidth_limit.response_enabled"));

  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_EQ(5, findGauge("test.http_bandwidth_limit.response_incoming_size"));
  EXPECT_EQ(5, findCounter("test.http_bandwidth_limit.response_incoming_total_size"));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual("hello"), false));
  token_timer->invokeCallback();
  EXPECT_EQ(0, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(5, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(5, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Send 1126 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1126, 'a'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));
  EXPECT_EQ(1126, findGauge("test.http_bandwidth_limit.response_incoming_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.response_incoming_total_size"));

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_EQ(1, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(1126, findGauge("test.http_bandwidth_limit.response_incoming_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.response_incoming_total_size"));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(1029, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(2, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(1080, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(51, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(1131, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  // Fire timer, also advance time. No time enable because there is nothing
  // buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'b')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(51, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(1182, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in one shot with end_stream true which should go through and
  // end the stream.
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data4(std::string(1024, 'c'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data4, true));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.response_incoming_size"));
  EXPECT_EQ(2206, findCounter("test.http_bandwidth_limit.response_incoming_total_size"));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(1024, 'c')), false));
  token_timer->invokeCallback();
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_EQ(3, findCounter("test.http_bandwidth_limit.response_enforced"));
  EXPECT_EQ(1024, findGauge("test.http_bandwidth_limit.response_allowed_size"));
  EXPECT_EQ(2206, findCounter("test.http_bandwidth_limit.response_allowed_total_size"));

  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-delay-ms"));
  EXPECT_EQ("2150", trailers_.get_("test-bandwidth-response-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-filter-delay-ms"));
  EXPECT_EQ("150", trailers_.get_("test-bandwidth-response-filter-delay-ms"));

  filter_->onDestroy();
}

TEST_F(FilterTest, LimitOnDecodeAndEncode) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: true
    runtime_key: foo_key
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 1
  enable_response_trailers: true
  response_trailer_prefix: test
  )";
  setup(fmt::format(config_yaml, "1"));

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1050));
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  ON_CALL(encoder_filter_callbacks_, addEncodedTrailers()).WillByDefault(ReturnRef(trailers_));
  Event::MockTimer* request_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  Event::MockTimer* response_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(50UL, config_->fillInterval().count());

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send small amount of data from both sides which should be within initial
  // bucket limit.
  Buffer::OwnedImpl dec_data1("hello");
  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data1, false));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  request_timer->invokeCallback();

  Buffer::OwnedImpl enc_data1("world!");
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data1, false));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual("world!"), false));
  response_timer->invokeCallback();

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  // Send 1075 bytes of data on request path which is 1s + 1 refill cycle of
  // data. Send 102 bytes of data on response path which is 2 refill cycles of
  // data.
  Buffer::OwnedImpl dec_data2(std::string(1075, 'd'));
  Buffer::OwnedImpl enc_data2(std::string(102, 'e'));

  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data2, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data2, false));

  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'd')), false));
  request_timer->invokeCallback();

  // Encoder will not be able to write any bytes due to insufficient tokens.
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string("")), false));
  response_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), false));
  request_timer->invokeCallback();
  // Encoder will not be able to write any bytes due to insufficient tokens.
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string("")), false));
  response_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'e')), false));
  response_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(51, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'e')), false));
  response_timer->invokeCallback();

  // Fire timer, also advance time. No time enable because there is nothing
  // buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'b')), false));
  response_timer->invokeCallback();

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in total with end_stream true which should go through and end
  // the streams.
  Buffer::OwnedImpl enc_data4(std::string(960, 'e'));
  Buffer::OwnedImpl dec_data4(std::string(51, 'd'));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data4, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data4, true));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), true));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(960, 'e')), false));
  EXPECT_CALL(encoder_filter_callbacks_, continueEncoding());

  request_timer->invokeCallback();
  response_timer->invokeCallback();
  EXPECT_EQ("2200", trailers_.get_("test-bandwidth-request-delay-ms"));
  EXPECT_EQ("2200", trailers_.get_("test-bandwidth-response-delay-ms"));
  // Only waiting for 1 unit
  EXPECT_EQ("50", trailers_.get_("test-bandwidth-request-filter-delay-ms"));
  // Waiting for 4 units
  EXPECT_EQ("200", trailers_.get_("test-bandwidth-response-filter-delay-ms"));

  filter_->onDestroy();
}

TEST_F(FilterTest, WithTrailers) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: true
    runtime_key: foo_key
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 1
  response_trailer_prefix: test
  )";
  setup(fmt::format(config_yaml, "1"));

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1050));
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* request_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  Event::MockTimer* response_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(50UL, config_->fillInterval().count());

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl dec_data(std::string(102, 'd'));
  Buffer::OwnedImpl enc_data(std::string(56, 'e'));

  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));

  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  request_timer->invokeCallback();
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), false));
  request_timer->invokeCallback();
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.request_pending"));

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'e')), false));
  response_timer->invokeCallback();
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));

  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(5, 'e')), false));
  response_timer->invokeCallback();
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.response_pending"));
  // No delay triggers since enable_response_trailers is false by default
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-response-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-request-filter-delay-ms"));
  EXPECT_EQ(false, response_trailers_.has("test-bandwidth-response-filter-delay-ms"));
}

TEST_F(FilterTest, WithTrailersNoEndStream) {
  constexpr absl::string_view config_yaml = R"(
  stat_prefix: test
  runtime_enabled:
    default_value: true
    runtime_key: foo_key
  enable_mode: REQUEST_AND_RESPONSE
  limit_kbps: 1
  enable_response_trailers: true
  )";
  setup(fmt::format(config_yaml, "1"));

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1050));
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* request_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  Event::MockTimer* response_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(50UL, config_->fillInterval().count());

  EXPECT_EQ(Http::Filter1xxHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  Buffer::OwnedImpl dec_data(std::string(102, 'd'));
  Buffer::OwnedImpl enc_data(std::string(56, 'e'));

  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data, false));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));

  EXPECT_CALL(*request_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), false));
  request_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'd')), false));
  request_timer->invokeCallback();
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.request_pending"));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.request_pending"));

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*response_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(51, 'e')), false));
  response_timer->invokeCallback();

  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(5, 'e')), false));
  response_timer->invokeCallback();
  EXPECT_EQ(1, findGauge("test.http_bandwidth_limit.response_pending"));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(0, findGauge("test.http_bandwidth_limit.response_pending"));

  EXPECT_EQ("50", response_trailers_.get_("bandwidth-request-delay-ms"));
  EXPECT_EQ("150", response_trailers_.get_("bandwidth-response-delay-ms"));
  EXPECT_EQ("50", response_trailers_.get_("bandwidth-request-filter-delay-ms"));
  EXPECT_EQ("50", response_trailers_.get_("bandwidth-response-filter-delay-ms"));
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
