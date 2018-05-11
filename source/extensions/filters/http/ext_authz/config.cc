#include "extensions/filters/http/ext_authz/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/http/ext_authz/v2alpha/ext_authz.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/common/ext_authz/ext_authz_impl.h"
#include "extensions/filters/http/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

Http::FilterFactoryCb ExtAuthzFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  auto filter_config =
      std::make_shared<FilterConfig>(proto_config, context.localInfo(), context.scope(),
                                     context.runtime(), context.clusterManager());
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 200);

  return [ grpc_service = proto_config.grpc_service(), &context, filter_config,
           timeout_ms ](Http::FilterChainFactoryCallbacks & callbacks) {
    auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            grpc_service, context.scope(), true);
    auto client = std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
        async_client_factory->create(), std::chrono::milliseconds(timeout_ms));
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{
        std::make_shared<Filter>(filter_config, std::move(client))});
  };
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ExtAuthzFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
