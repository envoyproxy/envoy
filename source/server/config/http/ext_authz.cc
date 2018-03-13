#include "server/config/http/ext_authz.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/ext_authz/ext_authz_impl.h"
#include "common/http/filter/ext_authz.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

HttpFilterFactoryCb ExtAuthzFilterConfig::createFilter(
    const envoy::config::filter::http::ext_authz::v2::ExtAuthz& proto_config, const std::string&,
    FactoryContext& context) {
  auto filter_config = std::make_shared<Http::ExtAuthz::FilterConfig>(
      proto_config, context.localInfo(), context.scope(), context.runtime(),
      context.clusterManager());
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 200);

  return [ grpc_service = proto_config.grpc_service(), &context, filter_config,
           timeout_ms ](Http::FilterChainFactoryCallbacks & callbacks) {
    auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(grpc_service,
                                                                                context.scope());
    auto client = std::make_unique<Envoy::ExtAuthz::GrpcClientImpl>(
        async_client_factory->create(), std::chrono::milliseconds(timeout_ms));
    callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{
        std::make_shared<Http::ExtAuthz::Filter>(filter_config, std::move(client))});
  };
}

HttpFilterFactoryCb ExtAuthzFilterConfig::createFilterFactory(const Json::Object&,
                                                              const std::string&, FactoryContext&) {
  NOT_IMPLEMENTED;
}

HttpFilterFactoryCb
ExtAuthzFilterConfig::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                   const std::string& stats_prefix,
                                                   FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::config::filter::http::ext_authz::v2::ExtAuthz&>(
          proto_config),
      stats_prefix, context);
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ExtAuthzFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
