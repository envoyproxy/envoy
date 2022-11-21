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

// A filter returns StopAllIterationAndBuffer or StopAllIterationAndWatermark for headers. The
// iteration continues after 5s.
class EncodeHeadersReturnStopAllFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-headers-return-stop-all-filter";

  // Returns Http::FilterHeadersStatus::StopAllIterationAndBuffer or
  // Http::FilterHeadersStatus::StopAllIterationAndWatermark for headers. Triggers a timer to
  // continue iteration after 5s.
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& header_map, bool) override {
    const auto entry_content = header_map.get(Envoy::Http::LowerCaseString("content_size"));
    const auto entry_added = header_map.get(Envoy::Http::LowerCaseString("added_size"));
    ASSERT(!entry_content.empty() && !entry_added.empty());
    content_size_ = std::stoul(std::string(entry_content[0]->value().getStringView()));
    added_size_ = std::stoul(std::string(entry_added[0]->value().getStringView()));

    createTimerForContinue();

    Http::MetadataMap metadata_map = {{"headers", "headers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));

    const auto entry_buffer = header_map.get(Envoy::Http::LowerCaseString("buffer_limit"));
    if (entry_buffer.empty()) {
      return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
    } else {
      watermark_enabled_ = true;
      encoder_callbacks_->setEncoderBufferLimit(
          std::stoul(std::string(entry_buffer[0]->value().getStringView())));
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    ASSERT(timer_triggered_);
    if (watermark_enabled_) {
      // High watermark reached before all data are received. The rest of the data is sent after
      // iteration resumes.
      EXPECT_LT(data.length(), content_size_);
    } else {
      // encodeData will only be called once after iteration resumes.
      EXPECT_EQ(data.length(), content_size_);
    }
    Http::MetadataMap metadata_map = {{"data", "data"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));

    Buffer::OwnedImpl added_data(std::string(added_size_, 'a'));
    encoder_callbacks_->addEncodedData(added_data, false);
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    ASSERT(timer_triggered_);
    Http::MetadataMap metadata_map = {{"trailers", "trailers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));

    Buffer::OwnedImpl data(std::string(added_size_, 'a'));
    encoder_callbacks_->addEncodedData(data, false);
    return Http::FilterTrailersStatus::Continue;
  }

private:
  // Creates a timer to continue iteration after 5s.
  void createTimerForContinue() {
    delay_timer_ = encoder_callbacks_->dispatcher().createTimer([this]() -> void {
      timer_triggered_ = true;
      encoder_callbacks_->continueEncoding();
    });
    delay_timer_->enableTimer(std::chrono::seconds(5));
  }

  Event::TimerPtr delay_timer_;
  bool timer_triggered_ = false;
  size_t added_size_ = 0;
  size_t content_size_ = 0;
  bool watermark_enabled_ = false;
};

constexpr char EncodeHeadersReturnStopAllFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<EncodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<EncodeHeadersReturnStopAllFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
