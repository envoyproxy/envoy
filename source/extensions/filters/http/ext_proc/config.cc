#include "extensions/filters/http/ext_proc/config.h"

#include <string>

#include "extensions/filters/http/ext_proc/ext_proc.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

Http::FilterFactoryCb ExternalProcessingFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ext_proc::v3alpha::ExternalProcessor& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  const auto filter_config = std::make_shared<FilterConfig>(proto_config);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{std::make_shared<Filter>(filter_config)});
  };
}

REGISTER_FACTORY(ExternalProcessingFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.ext_proc"};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy