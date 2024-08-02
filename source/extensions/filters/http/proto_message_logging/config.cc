#include "source/extensions/filters/http/proto_message_logging/config.h"

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/extensions/filters/http/proto_message_logging/extractor_impl.h"
#include "source/extensions/filters/http/proto_message_logging/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

FilterFactoryCreator::FilterFactoryCreator() : FactoryBase(kFilterName) {}

Envoy::Http::FilterFactoryCb
FilterFactoryCreator::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::proto_message_logging::v3::
        ProtoMessageLoggingConfig& proto_config,
    const std::string&, Envoy::Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterConfig>(
      proto_config, std::make_unique<ExtractorFactoryImpl>(),
      context.serverFactoryContext().api());
  return [filter_config](
             Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(*filter_config));
  };
}

REGISTER_FACTORY(FilterFactoryCreator,
                 Envoy::Server::Configuration::NamedHttpFilterConfigFactory);

}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
