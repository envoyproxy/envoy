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

// A filter returns StopAllIterationAndBuffer for headers. The iteration continues after 1s.
class DecodeHeadersReturnStopAllFilter2 : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-filter-2";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndBuffer for headers. Triggers a timer to
  // continue iteration after 1s.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    createTimerForContinue();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // Request data (size 70000) and added data by DecodeHeadersReturnStopAllFilter (size 1) are
    // received together.
    ASSERT(timer_triggered_);
    EXPECT_TRUE(data.length() == 70001 || data.length() == 70002);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    ASSERT(timer_triggered_);
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after 1s.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      timer_triggered_ = true;
      decoder_callbacks_->continueDecoding();
    });
    delay_timer_->enableTimer(std::chrono::seconds(1));
  }

  Event::TimerPtr delay_timer_;
  bool timer_triggered_ = false;
};

constexpr char DecodeHeadersReturnStopAllFilter2::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter2>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
