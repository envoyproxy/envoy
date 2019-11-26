#include "extensions/filters/http/grpc_web/config.h"

#include "envoy/config/filter/http/grpc_web/v2/grpc_web.pb.h"
#include "envoy/config/filter/http/grpc_web/v2/grpc_web.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_web/grpc_web_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

Http::FilterFactoryCb GrpcWebFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::grpc_web::v2::GrpcWeb&,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcWebFilter>(factory_context.grpcContext()));
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcWebFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
