#include <chrono>
#include <memory>

#include "envoy/event/dispatcher.h"

#include "common/http/filter/buffer_filter.h"
#include "common/http/header_map_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Http {

class BufferFilterTest : public testing::Test {
public:
  BufferFilterTest()
      : config_{new BufferFilterConfig{BufferFilter::generateStats("", store_), 1024 * 1024,
                                       std::chrono::seconds(0)}},
        filter_(config_) {
    filter_.setDecoderFilterCallbacks(callbacks_);
  }

  void expectTimerCreate() { timer_ = new NiceMock<Event::MockTimer>(&callbacks_.dispatcher_); }

  NiceMock<MockStreamDecoderFilterCallbacks> callbacks_;
  Stats::IsolatedStoreImpl store_;
  std::shared_ptr<BufferFilterConfig> config_;
  BufferFilter filter_;
  Event::MockTimer* timer_{};
};

TEST_F(BufferFilterTest, HeaderOnlyRequest) {
  TestHeaderMapImpl headers;
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_.decodeHeaders(headers, true));
}

TEST_F(BufferFilterTest, RequestWithData) {
  InSequence s;

  expectTimerCreate();

  TestHeaderMapImpl headers;
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data2, true));
}

TEST_F(BufferFilterTest, RequestTimeout) {
  InSequence s;

  expectTimerCreate();

  TestHeaderMapImpl headers;
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  TestHeaderMapImpl response_headers{
      {":status", "408"}, {"content-length", "22"}, {"content-type", "text/plain"}};
  EXPECT_CALL(callbacks_, encodeHeaders_(HeaderMapEqualRef(&response_headers), false));
  EXPECT_CALL(callbacks_, encodeData(_, true));
  timer_->callback_();

  filter_.onDestroy();
  EXPECT_EQ(1U, config_->stats_.rq_timeout_.value());
}

TEST_F(BufferFilterTest, TxResetAfterEndStream) {
  InSequence s;

  expectTimerCreate();

  TestHeaderMapImpl headers;
  EXPECT_EQ(FilterHeadersStatus::StopIteration, filter_.decodeHeaders(headers, false));

  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(FilterDataStatus::StopIterationAndBuffer, filter_.decodeData(data1, false));

  Buffer::OwnedImpl data2(" world");
  EXPECT_EQ(FilterDataStatus::Continue, filter_.decodeData(data2, true));

  // It's possible that the stream will be reset on the TX side even after RX end stream. Mimic
  // that here.
  filter_.onDestroy();
}

} // namespace Http
} // namespace Envoy
