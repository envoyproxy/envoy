#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

// A filter tests response metadata consuming and inserting. The filter inserts new
// metadata when encodeHeaders/Data/Trailers/100ContinueHeaders() are called. If
// the received metadata with key "consume", the metadata will be consumed and
// not forwarded to the next hop.
class ResponseMetadataStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    encoder_callbacks_->addEncodedMetadata().emplace("headers", "headers");
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    encoder_callbacks_->addEncodedMetadata().emplace("data", "data");
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    encoder_callbacks_->addEncodedMetadata().emplace("trailers", "trailers");
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    encoder_callbacks_->addEncodedMetadata().emplace("100-continue", "100-continue");
    return Http::FilterHeadersStatus::Continue;
  }

  // Adds a new metadata. If metadata_map contains key "consume", consumes the metadata.
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    auto it = metadata_map.find("consume");
    if (it != metadata_map.end()) {
      metadata_map.erase("consume");
    }
    encoder_callbacks_->addEncodedMetadata().emplace("metadata", "metadata");
    return Http::FilterMetadataStatus::Continue;
  }
};

class AddMetadataStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddMetadataStreamFilterConfig() : EmptyHttpFilterConfig("response-metadata-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ResponseMetadataStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddMetadataStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
