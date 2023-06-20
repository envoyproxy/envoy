#include "envoy/event/dispatcher.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/extensions/filters/http/common/stream_rate_limiter.h"

#include "test/common/http/common.h"
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
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {

class StreamRateLimiterTest : public testing::Test {
public:
  void setUpTest(uint16_t limit_kbps, uint16_t fill_interval,
                 std::shared_ptr<TokenBucket> token_bucket = nullptr) {
    EXPECT_CALL(decoder_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(decoder_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());

    limiter_ = std::make_unique<StreamRateLimiter>(
        limit_kbps, decoder_callbacks_.decoderBufferLimit(),
        [this] { decoder_callbacks_.onDecoderFilterAboveWriteBufferHighWatermark(); },
        [this] { decoder_callbacks_.onDecoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          decoder_callbacks_.injectDecodedDataToFilterChain(data, end_stream);
        },
        [this] { decoder_callbacks_.continueDecoding(); },
        [](uint64_t /*len*/, bool, std::chrono::milliseconds) {
          // config->stats().decode_allowed_size_.set(len);
        },
        time_system_, decoder_callbacks_.dispatcher_, decoder_callbacks_.scope(), token_bucket,
        std::chrono::milliseconds(fill_interval));
  }

  void setUpTest(uint16_t limit_kbps) {
    EXPECT_CALL(decoder_callbacks_.dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(decoder_callbacks_.dispatcher_, popTrackedObject(_)).Times(AnyNumber());

    limiter_ = std::make_unique<StreamRateLimiter>(
        limit_kbps, decoder_callbacks_.decoderBufferLimit(),
        [this] { decoder_callbacks_.onDecoderFilterAboveWriteBufferHighWatermark(); },
        [this] { decoder_callbacks_.onDecoderFilterBelowWriteBufferLowWatermark(); },
        [this](Buffer::Instance& data, bool end_stream) {
          decoder_callbacks_.injectDecodedDataToFilterChain(data, end_stream);
        },
        [this] { decoder_callbacks_.continueDecoding(); },
        [](uint64_t /*len*/, bool, std::chrono::milliseconds) {
          // config->stats().decode_allowed_size_.set(len);
        },
        time_system_, decoder_callbacks_.dispatcher_, decoder_callbacks_.scope());
  }

  uint64_t fillInterval() { return limiter_->fill_interval_.count(); }

  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Runtime::MockLoader> runtime_;
  std::unique_ptr<StreamRateLimiter> limiter_;
  Buffer::OwnedImpl data_;
  Event::SimulatedTimeSystem time_system_;
};

TEST_F(StreamRateLimiterTest, RateLimitOnSingleStream) {
  ON_CALL(decoder_callbacks_, decoderBufferLimit()).WillByDefault(Return(1100));
  Event::MockTimer* token_timer = new NiceMock<Event::MockTimer>(&decoder_callbacks_.dispatcher_);
  setUpTest(1);

  EXPECT_EQ(50UL, fillInterval());

  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  limiter_->writeData(data1, false);
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  token_timer->invokeCallback();

  // Advance time by 1s which should refill all tokens.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Send 1126 bytes of data which is 1s + 2 refill cycles of data.
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data2(std::string(1126, 'a'));
  limiter_->writeData(data2, false);

  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();

  // Get new data with current data buffered, not end_stream.
  Buffer::OwnedImpl data3(std::string(51, 'b'));
  limiter_->writeData(data3, false);

  // Fire timer, also advance time.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(50), _));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'a')), false));
  token_timer->invokeCallback();

  // Fire timer, also advance time. No timer enable because there is nothing
  // buffered.
  time_system_.advanceTimeWait(std::chrono::milliseconds(50));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(51, 'b')), false));
  token_timer->invokeCallback();

  // Advance time by 1s for a full refill.
  time_system_.advanceTimeWait(std::chrono::seconds(1));

  // Now send 1024 in one shot with end_stream true which should go through and
  // end the stream.
  EXPECT_CALL(*token_timer, enableTimer(std::chrono::milliseconds(0), _));
  Buffer::OwnedImpl data4(std::string(1024, 'c'));
  limiter_->writeData(data4, true);
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'c')), true));
  token_timer->invokeCallback();

  limiter_->destroy();
  EXPECT_EQ(limiter_->destroyed(), true);
}

} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
