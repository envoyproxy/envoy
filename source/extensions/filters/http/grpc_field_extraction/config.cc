#include "source/extensions/filters/http/grpc_field_extraction/config.h"

#include <memory>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"
#include "source/extensions/filters/http/grpc_field_extraction/filter.h"

#include "proto_field_extraction/message_data/cord_message_data.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

Envoy::Http::FilterFactoryCb FilterFactoryCreator::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_field_extraction::v3::GrpcFieldExtractionConfig&
        proto_config,
    const std::string&, Envoy::Server::Configuration::FactoryContext& context) {
  auto extractor_factory = std::make_shared<ExtractorFactoryImpl>();
  auto filter_config =
      std::make_shared<FilterConfig>(proto_config, *extractor_factory, context.api());
  return [filter_config, extractor_factory](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Filter>(*filter_config));
  };
}

REGISTER_FACTORY(FilterFactoryCreator, Envoy::Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
