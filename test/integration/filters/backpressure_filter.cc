#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter that buffers the entire request/response, then doubles
// the content of the filter buffer.
class BackpressureFilter : public Http::PassThroughFilter {
public:
  void onDestroy() override {
    if (!below_write_buffer_low_watermark_called_) {
      decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    if (end_stream) {
      below_write_buffer_low_watermark_called_ = true;
      decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }
    return Http::FilterDataStatus::Continue;
  }

private:
  bool below_write_buffer_low_watermark_called_{false};
};

class BackpressureConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  BackpressureConfig() : EmptyHttpFilterConfig("backpressure-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::BackpressureFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<BackpressureConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
