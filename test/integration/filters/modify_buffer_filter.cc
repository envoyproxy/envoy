#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter that buffers the entire request/response, then doubles
// the content of the filter buffer.
class ModifyBufferStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    decoder_callbacks_->addDecodedData(data, true);

    if (end_stream) {
      decoder_callbacks_->modifyDecodingBuffer([](auto& buffer) {
        // Append the buffer with itself.
        buffer.add(buffer);
      });
      return Http::FilterDataStatus::Continue;
    }

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    encoder_callbacks_->addEncodedData(data, true);

    if (end_stream) {
      encoder_callbacks_->modifyEncodingBuffer([](auto& buffer) {
        // Append the buffer with itself.
        buffer.add(buffer);
      });
      return Http::FilterDataStatus::Continue;
    }

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
};

class ModifyBuffferFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  ModifyBuffferFilterConfig() : EmptyHttpFilterConfig("modify-buffer-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ModifyBufferStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<ModifyBuffferFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
