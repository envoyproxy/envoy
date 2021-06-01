#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that inserts trailers at the end of encode/decode
class AddTrailersStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    if (end_stream) {
      decoder_callbacks_->addDecodedTrailers().addCopy(Http::LowerCaseString("grpc-message"),
                                                       "decode");
    }

    return Http::FilterDataStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    if (end_stream) {
      encoder_callbacks_->addEncodedTrailers().setGrpcMessage("encode");
    }

    return Http::FilterDataStatus::Continue;
  }
};

class AddTrailersStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddTrailersStreamFilterConfig() : EmptyHttpFilterConfig("add-trailers-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::AddTrailersStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddTrailersStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
