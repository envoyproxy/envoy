#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

// A filter tests response metadata inserting. The filter inserts new
// metadata when encodeHeaders/Data/Trailers/100ContinueHeaders/Metadata() are called.
class ResponseMetadataInsertStreamFilter : public Http::PassThroughFilter {
public:
  // Inserts one new metadata_map.
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {
        {"headers", "headers"}, {"duplicate", "duplicate"}, {"remove", "remove"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  // Inserts one new metadata_map.
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    Http::MetadataMap metadata_map = {
        {"data", "data"}, {"duplicate", "duplicate"}, {"remove", "remove"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    return Http::FilterDataStatus::Continue;
  }

  // Inserts two metadata_maps by calling decoder_callbacks_->encodeMetadata() twice.
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    Http::MetadataMap metadata_map = {{"trailers", "trailers"}, {"remove", "remove"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    return Http::FilterTrailersStatus::Continue;
  }

  // Inserts two metadata_maps by calling decoder_callbacks_->encodeMetadata() twice.
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    Http::MetadataMap metadata_map = {
        {"100-continue", "100-continue"}, {"duplicate", "duplicate"}, {"remove", "remove"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  // Uses two ways to insert new metadata: (1) add new metadata to metadata_map directly. (2) add
  // new metadata by calling decoder_callbacks_->encodeMetadata() twice.
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    // Add new metadata to metadata_map directly.
    metadata_map.emplace("keep", "keep");
    // Add new metadata by calling encodeMetadata().
    Http::MetadataMap new_metadata_map = {{"metadata", "metadata"}};
    Http::MetadataMapPtr new_metadata_map_ptr = std::make_unique<Http::MetadataMap>(new_metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(new_metadata_map_ptr));
    new_metadata_map = {{"duplicate", "duplicate"}};
    new_metadata_map_ptr = std::make_unique<Http::MetadataMap>(new_metadata_map);
    decoder_callbacks_->encodeMetadata(std::move(new_metadata_map_ptr));
    return Http::FilterMetadataStatus::Continue;
  }
};

class AddMetadataInsertStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddMetadataInsertStreamFilterConfig()
      : EmptyHttpFilterConfig("response-metadata-insert-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ResponseMetadataInsertStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddMetadataInsertStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
