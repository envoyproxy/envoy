#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {

// A filter tests response metadata consuming and proxy.
class ResponseMetadataConsumeStreamFilter : public Http::PassThroughFilter {
public:
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    auto it = metadata_map.find("consume");
    if (it != metadata_map.end()) {
      metadata_map.erase("consume");
      metadata_map.emplace("replace", "replace");
    }
    it = metadata_map.find("remove");
    if (it != metadata_map.end()) {
      metadata_map.erase("remove");
    }
    metadata_map.emplace("metadata", "metadata");
    return Http::FilterMetadataStatus::Continue;
  }
};

class AddMetadataConsumeStreamFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  AddMetadataConsumeStreamFilterConfig()
      : EmptyHttpFilterConfig("response-metadata-consume-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::ResponseMetadataConsumeStreamFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<AddMetadataConsumeStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
