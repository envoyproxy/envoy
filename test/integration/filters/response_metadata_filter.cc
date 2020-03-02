#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter tests response metadata process. The filter inserts new
// metadata when encodeHeaders/Data/Trailers/100ContinueHeaders/Metadata() are called, and consumes
// metadata in encodeMetadata().
class ResponseMetadataStreamFilter : public Http::PassThroughFilter {
public:
  // Inserts one new metadata_map.
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {{"headers", "headers"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  // Inserts one new metadata_map.
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    Http::MetadataMap metadata_map = {{"data", "data"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    return Http::FilterDataStatus::Continue;
  }

  // Inserts two metadata_maps by calling decoder_callbacks_->encodeMetadata() twice.
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    Http::MetadataMap metadata_map = {{"trailers", "trailers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    return Http::FilterTrailersStatus::Continue;
  }

  // Inserts two metadata_maps by calling decoder_callbacks_->encodeMetadata() twice.
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    Http::MetadataMap metadata_map = {{"100-continue", "100-continue"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  // Adds new metadata to metadata_map directly, and consumes metadata when the keys are equal to
  // remove and metadata. If the key is equal to consume, replaces it with replace.
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    // Adds new metadata to metadata_map directly.
    metadata_map.emplace("keep", "keep");
    // Consumes metadata.
    auto it = metadata_map.find("consume");
    if (it != metadata_map.end()) {
      metadata_map.erase("consume");
      metadata_map.emplace("replace", "replace");
    }
    it = metadata_map.find("metadata");
    if (it != metadata_map.end()) {
      metadata_map.erase("metadata");
    }
    return Http::FilterMetadataStatus::Continue;
  }
};

class AddMetadataStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddMetadataStreamFilterConfig() : EmptyHttpFilterConfig("response-metadata-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
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
