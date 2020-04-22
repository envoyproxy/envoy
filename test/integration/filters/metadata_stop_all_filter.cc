#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

class MetadataStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "metadata-stop-all-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& header_map, bool) override {
    const Http::HeaderEntry* entry_content =
        header_map.get(Envoy::Http::LowerCaseString("content_size"));
    ASSERT(entry_content != nullptr);
    content_size_ = std::stoul(std::string(entry_content->value().getStringView()));

    createTimerForContinue();

    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    ASSERT(timer_triggered_);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    ASSERT(timer_triggered_);
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap&) override {
    ASSERT(timer_triggered_);
    return Http::FilterMetadataStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after conditions meet.
  void createTimerForContinue() {
    delay_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() -> void {
      if (content_size_ > 0 && decoder_callbacks_->streamInfo().bytesReceived() >= content_size_) {
        timer_triggered_ = true;
        decoder_callbacks_->continueDecoding();
      } else {
        // Creates a new timer to try again later.
        createTimerForContinue();
      }
    });
    delay_timer_->enableTimer(std::chrono::milliseconds(50));
  }

  Event::TimerPtr delay_timer_;
  bool timer_triggered_ = false;
  size_t content_size_ = 0;
};

constexpr char MetadataStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<MetadataStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
