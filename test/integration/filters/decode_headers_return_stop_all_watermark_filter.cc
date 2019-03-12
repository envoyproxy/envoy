#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter returns StopAllIterationAndWatermark for headers. The iteration continues after 5s.
class DecodeHeadersReturnStopAllWatermarkFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-watermark-filter";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndWatermark for headers, and sets buffer
  // limit to 100.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    decoder_callbacks_->setDecoderBufferLimit(100);
    createTimerForContinue();
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // High watermark reached before all data are received. The rest of the data is sent after
    // iteration resumes.
    ASSERT(timer_triggered_);
    EXPECT_LT(data.length(), 70000);
    Buffer::OwnedImpl added_data("a");
    decoder_callbacks_->addDecodedData(added_data, false);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    ASSERT(timer_triggered_);
    Buffer::OwnedImpl data("a");
    decoder_callbacks_->addDecodedData(data, false);
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after 5s.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      timer_triggered_ = true;
      decoder_callbacks_->continueDecoding();
    });
    delay_timer_->enableTimer(std::chrono::seconds(5));
  }

  Event::TimerPtr delay_timer_;
  bool timer_triggered_ = false;
};

constexpr char DecodeHeadersReturnStopAllWatermarkFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllWatermarkFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
