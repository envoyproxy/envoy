#include "source/extensions/filters/http/proto_message_extraction/config.h"

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_extraction/filter.h"

#include "proto_field_extraction/message_data/cord_message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

FilterFactoryCreator::FilterFactoryCreator() : ExceptionFreeFactoryBase(kFilterName) {}

absl::StatusOr<Envoy::Http::FilterFactoryCb>
FilterFactoryCreator::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::proto_message_extraction::v3::
        ProtoMessageExtractionConfig& proto_config,
    const std::string&, Envoy::Server::Configuration::FactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config =
      std::make_shared<FilterConfig>(proto_config, std::make_unique<ExtractorFactoryImpl>(),
                                     context.serverFactoryContext().api(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return [filter_config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*filter_config));
  };
}

absl::StatusOr<Envoy::Http::FilterFactoryCb>
FilterFactoryCreator::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::proto_message_extraction::v3::
        ProtoMessageExtractionConfig& proto_config,
    const std::string&, Envoy::Server::Configuration::ServerFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<FilterConfig>(
      proto_config, std::make_unique<ExtractorFactoryImpl>(), context.api(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return [filter_config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*filter_config));
  };
}

REGISTER_FACTORY(FilterFactoryCreator, Envoy::Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
