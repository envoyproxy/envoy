#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter that buffers until the limit is reached and then continues.
class BufferContinueStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    response_headers_ = &headers;
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    data_total_ += data.length();

    const auto limit = encoder_callbacks_->encoderBufferLimit();
    const auto header_size = response_headers_->byteSize();

    if (limit && header_size + data_total_ > limit) {
      // Give up since we've reached the buffer limit, Envoy should generate
      // a 500 since it couldn't finished encoding.
      return Http::FilterDataStatus::Continue;
    }

    encoder_callbacks_->addEncodedData(data, false);

    if (!end_stream) {
      return Http::FilterDataStatus::StopIterationAndBuffer;
    }

    return Http::FilterDataStatus::Continue;
  }

private:
  Http::ResponseHeaderMap* response_headers_;
  uint64_t data_total_{0};
};

class BufferContinueFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  BufferContinueFilterConfig() : EmptyHttpDualFilterConfig("buffer-continue-filter") {}

  Http::FilterFactoryCb createDualFilter(const std::string&,
                                         Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::BufferContinueStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<BufferContinueFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<BufferContinueFilterConfig,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
