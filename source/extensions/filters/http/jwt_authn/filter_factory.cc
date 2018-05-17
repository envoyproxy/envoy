#include "extensions/filters/http/jwt_authn/filter_factory.h"

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/jwt_authn/data_store.h"
#include "extensions/filters/http/jwt_authn/filter.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

Http::FilterFactoryCb
FilterFactory::createFilterFactoryFromProtoTyped(const JwtAuthentication& proto_config,
                                                 const std::string&,
                                                 Server::Configuration::FactoryContext& context) {
  auto store_factory = std::make_shared<DataStoreFactory>(proto_config, context);
  return [store_factory](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Filter>(store_factory));
  };
}

/**
 * Static registration for this jwt_authn filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FilterFactory, Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
