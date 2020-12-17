#include "extensions/filters/http/kill_request/kill_request_config.h"

#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.h"
#include "envoy/extensions/filters/http/kill_request/v3/kill_request.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/kill_request/kill_request_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace KillRequest {

Http::FilterFactoryCb KillRequestFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::kill_request::v3::KillRequest& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  return [proto_config, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<KillRequestFilter>(proto_config, context.api().randomGenerator()));
  };
}

/**
 * Static registration for the KillRequest filter. @see RegisterFactory.
 */
REGISTER_FACTORY(KillRequestFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace KillRequest
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
