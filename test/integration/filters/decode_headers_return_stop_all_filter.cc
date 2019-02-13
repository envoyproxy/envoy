#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

// A filter returns StopAllTypesIteration for headers. The iteration continues when end_stream is
// received.
class DecodeHeadersReturnStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-filter";

  // Returns Http::FilterHeadersStatus::StopAllTypesIteration for headers.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::StopAllTypesIteration;
  }

  // Continues the iteration 2s after end_stream is received.
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    call_count_++;
    if (end_stream) {
      // Verifies decodeData() is called more than once.
      ASSERT(call_count_ > 1);
      continued_ = true;
      createTimerForContinue();
    }
    return Http::FilterDataStatus::Continue;
  }

  // Continues the iteration 2s after trailers are received.
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    if (!continued_) {
      // Verifies decodeData() is called more than once.
      ASSERT(call_count_ > 1);
      createTimerForContinue();
    }
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after 2s.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { decoder_callbacks_->continueDecoding(); });
    delay_timer_->enableTimer(std::chrono::seconds(2));
  }

  Event::TimerPtr delay_timer_;
  int call_count_ = 0;
  bool continued_ = false;
};

constexpr char DecodeHeadersReturnStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
