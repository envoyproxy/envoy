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
    Http::MetadataMap metadata_map = {{"headers", "headers"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    Http::MetadataMap metadata_map = {{"data", "data"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    Http::MetadataMap metadata_map = {{"trailers", "trailers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    Http::MetadataMap metadata_map = {{"100-continue", "100-continue"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    metadata_map = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  // If metadata_map contains key "consume", consumes the metadata, and replace it with a new one.
  // The function also adds a new metadata using addEncodedMetadata().
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    auto it = metadata_map.find("consume");
    if (it != metadata_map.end()) {
      metadata_map.erase("consume");
      metadata_map.emplace("replace", "replace");
    }
    Http::MetadataMap metadata_map_add = {{"metadata", "metadata"}, {"duplicate", "duplicate"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map_add);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
    metadata_map_add = {{"duplicate", "duplicate"}};
    metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map_add);
    encoder_callbacks_->addEncodedMetadata().emplace_back(std::move(metadata_map_ptr));
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
