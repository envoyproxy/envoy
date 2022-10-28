#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter returns StopAllIterationAndBuffer or StopAllIterationAndWatermark for headers. How the
// filter acts depends on the headers received.
class DecodeHeadersReturnStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-return-stop-all-filter";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndBuffer or
  // Http::FilterHeadersStatus::StopAllIterationAndWatermark for headers. Triggers a timer to
  // continue iteration after 5s.
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override {
    const auto entry_content = header_map.get(Envoy::Http::LowerCaseString("content_size"));
    const auto entry_added = header_map.get(Envoy::Http::LowerCaseString("added_size"));
    ASSERT(!entry_content.empty() && !entry_added.empty());
    content_size_ = std::stoul(std::string(entry_content[0]->value().getStringView()));
    added_size_ = std::stoul(std::string(entry_added[0]->value().getStringView()));
    const auto entry_is_first_trigger =
        header_map.get(Envoy::Http::LowerCaseString("is_first_trigger"));
    is_first_trigger_ = !entry_is_first_trigger.empty();
    // Remove "first_trigger" headers so that if the filter is registered twice in a filter chain,
    // it would act differently.
    header_map.remove(Http::LowerCaseString("is_first_trigger"));

    createTimerForContinue();

    const auto entry_buffer = header_map.get(Envoy::Http::LowerCaseString("buffer_limit"));
    if (entry_buffer.empty() || !is_first_trigger_) {
      return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
    } else {
      watermark_enabled_ = true;
      buffer_limit_ = std::stoul(std::string(entry_buffer[0]->value().getStringView()));
      decoder_callbacks_->setDecoderBufferLimit(buffer_limit_);
      header_map.remove(Http::LowerCaseString("buffer_limit"));
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    ASSERT(timer_triggered_);
    if (is_first_trigger_) {
      if (watermark_enabled_) {
        // High watermark reached before all data are received. The rest of the data is sent after
        // iteration resumes.
        EXPECT_LT(data.length(), content_size_);
      } else {
        // decodeData will only be called once after iteration resumes.
        EXPECT_EQ(data.length(), content_size_);
      }
      Buffer::OwnedImpl added_data(std::string(added_size_, 'a'));
      decoder_callbacks_->addDecodedData(added_data, false);
    } else {
      EXPECT_TRUE(data.length() == content_size_ + added_size_ ||
                  data.length() == content_size_ + added_size_ * 2);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    ASSERT(timer_triggered_);
    if (is_first_trigger_) {
      Buffer::OwnedImpl data(std::string(added_size_, 'a'));
      decoder_callbacks_->addDecodedData(data, false);
    }
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after conditions meet.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      // If decodeHeaders() returns StopAllIterationAndBuffer, triggers the timer when all the
      // request data has been received. If decodeHeaders() returns StopAllIterationAndWatermark,
      // triggers the timer when received data exceed buffer limit.
      if ((content_size_ > 0 &&
           decoder_callbacks_->streamInfo().bytesReceived() >= content_size_) ||
          (watermark_enabled_ && buffer_limit_ > 0 &&
           decoder_callbacks_->streamInfo().bytesReceived() >= buffer_limit_)) {
        timer_triggered_ = true;
        decoder_callbacks_->continueDecoding();
      } else {
        // Create a new timer to try again later.
        createTimerForContinue();
      }
    });
    delay_timer_->enableTimer(std::chrono::milliseconds(500));
  }

  Event::TimerPtr delay_timer_;
  bool timer_triggered_ = false;
  size_t content_size_ = 0;
  size_t added_size_ = 0;
  size_t buffer_limit_ = 0;
  bool watermark_enabled_ = false;
  bool is_first_trigger_ = false;
};

constexpr char DecodeHeadersReturnStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<DecodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
