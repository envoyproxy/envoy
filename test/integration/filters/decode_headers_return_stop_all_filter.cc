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

// A filter returns StopAllIterationAndBuffer for headers. The iteration continues after 5s.
class DecodeHeadersReturnStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-filter";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndBuffer for headers. Triggers a timer to
  // continue iteration after 5s.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& header_map, bool) override {
    Http::HeaderEntry* entry_content = header_map.get(Envoy::Http::LowerCaseString("content_size"));
    Http::HeaderEntry* entry_added = header_map.get(Envoy::Http::LowerCaseString("added_size"));
    ASSERT(entry_content != nullptr && entry_added != nullptr);
    content_size_ = std::stoul(std::string(entry_content->value().getStringView()));
    added_size_ = std::stoul(std::string(entry_added->value().getStringView()));
    createTimerForContinue();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    // decodeData will only be called once after iteration resumes.
    ASSERT(timer_triggered_);
    EXPECT_EQ(data.length(), content_size_);
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
  size_t content_size_ = 0;
  size_t added_size_ = 0;
};

constexpr char DecodeHeadersReturnStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
