#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

//
// Change this when ext_proc test changed.
uint32_t kBackpressureFilterHighWatermark = 64 * 1024;

// Back pressure filter specifically tailed for ext_proc streaming mode and sidestream injection.
class EncodeBackpressureFilter : public Http::PassThroughFilter {
public:
  void onDestroy() override {
    if (!below_write_buffer_low_watermark_called_) {
      decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    // LOG(INFO) << "tyxia_decodeData: " << data.length() << ", end_stream =" << end_stream << "\n";
    // Precisely control the back pressure.
    // In ext_proc streaming mode, empty request body will be forwarded to upstream
    // until the ext_proc server's response is returned. (i.e., mutate the request body.)
    // Thus, here the back pressure will only be applied on the first response from sidestream.
    if (data.length() >= kBackpressureFilterHighWatermark &&
        !above_write_buffer_high_watermark_called_) {
      // LOG(INFO) << "tyxia_above_write_buffer_high_watermar\n";
      above_write_buffer_high_watermark_called_ = true;
      below_write_buffer_low_watermark_called_ = false;
      decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    }

    if (end_stream) {
      if (above_write_buffer_high_watermark_called_) {
        // LOG(INFO) << "tyxia_below_write_buffer_low_watermar\n";
        decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
        below_write_buffer_low_watermark_called_ = true;
        above_write_buffer_high_watermark_called_ = false;
      }
      return Http::FilterDataStatus::Continue;
    } else {
      return Http::FilterDataStatus::StopIterationAndWatermark;
    }
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    // LOG(INFO) << "tyxia_encodeData: " << data.length() << ", end_stream =" << end_stream << "\n";
    // Back pressure to upstream
    // We need to trigger it here rather than under if >= kBackpressureFilterHighWatermark
    // because at later time, upstream response is done and only sidestream injection remains,
    // no watermark callback for upstream.
    if (counter == 0) {
      above_write_buffer_high_watermark_called_ = true;
      below_write_buffer_low_watermark_called_ = false;
      encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
      counter++;
    } else if (counter == 1) {
      below_write_buffer_low_watermark_called_ = true;
      above_write_buffer_high_watermark_called_ = false;
      encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
      counter++;
    }

    // Back pressure to sidestream. Here the original upstream response is done.
    if (data.length() >= kBackpressureFilterHighWatermark &&
        !above_write_buffer_high_watermark_called_) {
      // LOG(INFO) << "tyxia_above_write_buffer_high_watermar\n";
      above_write_buffer_high_watermark_called_ = true;
      below_write_buffer_low_watermark_called_ = false;
      encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
    }

    if (end_stream) {
      if (above_write_buffer_high_watermark_called_) {
        below_write_buffer_low_watermark_called_ = true;
        above_write_buffer_high_watermark_called_ = false;
        encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
      }
      return Http::FilterDataStatus::Continue;
    } else {
      return Http::FilterDataStatus::StopIterationAndWatermark;
    }
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    return Http::FilterTrailersStatus::Continue;
  }

private:
  int counter = 0;
  bool below_write_buffer_low_watermark_called_{false};
  bool above_write_buffer_high_watermark_called_{false};
};

class BackpressureConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  BackpressureConfig() : EmptyHttpFilterConfig("backpressure-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::EncodeBackpressureFilter>());
    };
  }
};

// Perform static registration
static Registry::RegisterFactory<BackpressureConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy