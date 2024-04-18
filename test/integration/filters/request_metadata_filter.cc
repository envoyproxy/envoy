#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {
// A filter tests request metadata consuming and inserting. The filter inserts new
// metadata when decodeHeaders/Data/Trailers() are called. If the received metadata with key
// "consume", the metadata will be consumed and not forwarded to the next hop.
class RequestMetadataStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    Http::MetadataMap metadata_map = {{"headers", "headers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->addDecodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    Http::MetadataMap metadata_map = {{"data", "data"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->addDecodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    Http::MetadataMap metadata_map = {{"trailers", "trailers"}};
    Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
    decoder_callbacks_->addDecodedMetadata().emplace_back(std::move(metadata_map_ptr));
    return Http::FilterTrailersStatus::Continue;
  }

  // If metadata_map contains key "consume", consumes the metadata, and replace it with a new one.
  // The function also adds a new metadata using addDecodedMetadata().
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override {
    auto it = metadata_map.find("consume");
    if (it != metadata_map.end()) {
      metadata_map.erase("consume");
      metadata_map.emplace("replace", "replace");
    }
    metadata_map["metadata"] = "metadata";
    return Http::FilterMetadataStatus::Continue;
  }
};

class AddRequestMetadataStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddRequestMetadataStreamFilterConfig() : EmptyHttpFilterConfig("request-metadata-filter") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::RequestMetadataStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddRequestMetadataStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy
