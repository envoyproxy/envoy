#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter that buffers the entire request/response.
class EncoderDecoderBufferStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndBuffer;
  }
};

class EncoderDecoderBuffferFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  EncoderDecoderBuffferFilterConfig() : EmptyHttpFilterConfig("encoder-decoder-buffer-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::EncoderDecoderBufferStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<EncoderDecoderBuffferFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
