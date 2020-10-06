#include "extensions/filters/http/cdn_loop/config.h"

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/cdn_loop/v3alpha/cdn_loop.pb.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "common/common/statusor.h"

#include "extensions/filters/http/cdn_loop/filter.h"
#include "extensions/filters/http/cdn_loop/parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {

using ::Envoy::Extensions::HttpFilters::CdnLoop::Parser::parseCdnId;
using ::Envoy::Extensions::HttpFilters::CdnLoop::Parser::ParseContext;
using ::Envoy::Extensions::HttpFilters::CdnLoop::Parser::ParsedCdnId;

Http::FilterFactoryCb CdnLoopFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cdn_loop::v3alpha::CdnLoopConfig& config,
    const std::string& /*stats_prefix*/, Server::Configuration::FactoryContext& /*context*/) {
  StatusOr<ParsedCdnId> context = parseCdnId(ParseContext(config.cdn_id()));
  if (!context.ok()) {
    throw EnvoyException(fmt::format("Provided cdn_id \"{}\" is not a valid CDN identifier: {}",
                                     config.cdn_id(), context.status()));
  }
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<CdnLoopFilter>(config.cdn_id(), config.max_allowed_occurrences()));
  };
}

REGISTER_FACTORY(CdnLoopFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
