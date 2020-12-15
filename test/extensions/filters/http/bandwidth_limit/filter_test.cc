#include "envoy/extensions/filters/http/bandwidth_limit/v3alpha/bandwidth_limit.pb.h"

#include "extensions/filters/http/bandwidth_limit/bandwidth_limit.h"

#include "test/mocks/http/mocks.h"

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
namespace BandwidthLimitFilter {

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  void setup(const std::string& yaml) {
    envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, stats_, runtime_, time_system_, true);
    filter_ = std::make_shared<BandwidthLimiter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_filter_callbacks_);
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_, name);
    return counter != nullptr ? counter->value() : 0;
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
};

TEST_F(FilterTest, Disabled) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: Disabled
  limit_kbps: 10
  fill_rate: 32
  )";
  setup(fmt::format(config_yaml, "1"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(0U, findCounter("test.http_bandwidth_limit.enabled"));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ(0U, findCounter("test.http_bandwidth_limit.enabled"));
}

TEST_F(FilterTest, BandwidthLimitOnDecode) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: Ingress
  limit_kbps: 1
  )";
  setup(fmt::format(config_yaml, "1"));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(16UL, config_->fill_rate());

  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, false));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  token_timer->invokeCallback();

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Send 1152 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1152, 'a'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data2, false));

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(64, 'a')), false));
  token_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(64, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(64, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time. No timer enable because there is nothing buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(64, 'b')), false));
  token_timer->invokeCallback();

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in one shot with end_stream true which should go through and end the stream.
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data4(std::string(1024, 'c'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data4, true));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'c')), true));
  token_timer->invokeCallback();

  filter_->onDestroy();
}

TEST_F(FilterTest, BandwidthLimitOnEncode) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: Egress
  limit_kbps: 1
  )";
  setup(fmt::format(config_yaml, "1"));
  EXPECT_CALL(encoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());

  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(16UL, config_->fill_rate());

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
}

TEST_F(FilterTest, BandwidthLimitOnDecodeAndEncode) {
  const std::string config_yaml = R"(
  stat_prefix: test
  enable_mode: IngressAndEgress
  limit_kbps: 1
  )";
  setup(fmt::format(config_yaml, "1"));
  EXPECT_CALL(decoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());
  EXPECT_CALL(encoder_filter_callbacks_.dispatcher_, setTrackedObject(_)).Times(AnyNumber());

  ON_CALL(decoder_filter_callbacks_, decoderBufferLimit()).WillByDefault(Return(1050));
  ON_CALL(encoder_filter_callbacks_, encoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* decode_timer =
      new NiceMock<Event::MockTimer>(&decoder_filter_callbacks_.dispatcher_);
  Event::MockTimer* encode_timer =
      new NiceMock<Event::MockTimer>(&encoder_filter_callbacks_.dispatcher_);

  EXPECT_EQ(1UL, config_->limit());
  EXPECT_EQ(16UL, config_->fill_rate());

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers_));
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  // Send small amount of data from both sides which should be within initial bucket limit.
  Buffer::OwnedImpl dec_data1("hello");
  EXPECT_CALL(*decode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data1, false));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  decode_timer->invokeCallback();

  Buffer::OwnedImpl enc_data1("world!");
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data1, false));

  // Encoder will not be able to write any bytes due to insufficient tokens.
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string("")), false));
  encode_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual("world!"), false));
  encode_timer->invokeCallback();

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  // Send 1088 bytes of data on request path which is 1s + 1 refill cycle of data.
  // Send 128 bytes of data on response path which is 2 refill cycles of data.
  Buffer::OwnedImpl dec_data2(std::string(1088, 'd'));
  Buffer::OwnedImpl enc_data2(std::string(128, 'e'));

  EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*decode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data2, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data2, false));

  EXPECT_CALL(*decode_timer, enableTimer(std::chrono::milliseconds(63), _));
  // EXPECT_CALL(decoder_filter_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'd')), false));
  decode_timer->invokeCallback();

  // Encoder will not be able to write any bytes due to insufficient tokens.
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string("")), false));
  encode_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(64, 'd')), false));
  decode_timer->invokeCallback();
  // Encoder will not be able to write any bytes due to insufficient tokens.
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string("")), false));
  encode_timer->invokeCallback();

  // Fire timer, also advance time by 1 unit.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'e')), false));
  encode_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(64, 'b'));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(63), _));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'e')), false));
  encode_timer->invokeCallback();

  // Fire timer, also advance time. No time enable because there is nothing buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(63));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(64, 'b')), false));
  encode_timer->invokeCallback();

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in total with end_stream true which should go through and end the streams.
  Buffer::OwnedImpl enc_data4(std::string(960, 'e'));
  Buffer::OwnedImpl dec_data4(std::string(64, 'd'));
  EXPECT_CALL(*encode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_CALL(*decode_timer, enableTimer(std::chrono::milliseconds(0), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(dec_data4, true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(enc_data4, true));
  EXPECT_CALL(decoder_filter_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(64, 'd')), true));
  EXPECT_CALL(encoder_filter_callbacks_,
              injectEncodedDataToFilterChain(BufferStringEqual(std::string(960, 'e')), true));
  encode_timer->invokeCallback();
  decode_timer->invokeCallback();

  filter_->onDestroy();
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
