#include "server/config/network/ext_authz.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.validate.h"
#include "envoy/ext_authz/ext_authz.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/ext_authz/ext_authz_impl.h"
#include "common/filter/ext_authz.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb ExtAuthzConfigFactory::createFilter(
    const envoy::config::filter::network::ext_authz::v2::ExtAuthz& proto_config,
    FactoryContext& context) {
  ExtAuthz::TcpFilter::ConfigSharedPtr ext_authz_config(
      new ExtAuthz::TcpFilter::Config(proto_config, context.scope()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 200);

  return [ grpc_service = proto_config.grpc_service(), &context, ext_authz_config,
           timeout_ms ](Network::FilterManager & filter_manager)
      ->void {

    auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(grpc_service,
                                                                                context.scope());

    auto client = std::make_unique<Envoy::ExtAuthz::GrpcClientImpl>(
        async_client_factory->create(), std::chrono::milliseconds(timeout_ms));
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        std::make_shared<ExtAuthz::TcpFilter::Instance>(ext_authz_config, std::move(client))});
  };
}

NetworkFilterFactoryCb ExtAuthzConfigFactory::createFilterFactory(const Json::Object&,
                                                                  FactoryContext&) {
  NOT_IMPLEMENTED;
}

NetworkFilterFactoryCb
ExtAuthzConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                    FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::ext_authz::v2::ExtAuthz&>(proto_config),
      context);
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ExtAuthzConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
