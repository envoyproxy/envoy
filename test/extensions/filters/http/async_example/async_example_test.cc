#include "source/extensions/filters/http/async_example/async_example.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AsyncExample {

class AsyncExampleFilterTest : public testing::Test {
public:
  AsyncExampleFilterTest() {
    config_ = std::make_shared<AsyncExampleConfig>(
        envoy::extensions::filters::http::async_example::v3::AsyncExample());
    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    // MockTimer constructor sets up EXPECT_CALL for createTimer_
    filter_ = std::make_unique<AsyncExampleFilter>(config_, dispatcher_);
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  AsyncExampleConfigSharedPtr config_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* timer_; // Owned by filter_
  std::unique_ptr<AsyncExampleFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks callbacks_;
};

TEST_F(AsyncExampleFilterTest, DecodeDataPausesAndResumes) {
  Buffer::OwnedImpl data("hello");
  
  // First call should pause
  EXPECT_CALL(*timer_, enableTimer(std::chrono::milliseconds(1000), _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(data, false));

  // Simulate timer firing
  EXPECT_CALL(callbacks_, continueDecoding());
  timer_->invokeCallback();

  // Second call (re-entry or next chunk) should NOT pause because async_complete_ is set
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, true));
}

} // namespace AsyncExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
