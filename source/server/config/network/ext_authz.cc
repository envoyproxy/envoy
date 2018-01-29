#include "server/config/network/ext_authz.h"

#include <chrono>
#include <string>

#include "envoy/api/v2/filter/network/ext_authz.pb.validate.h"
#include "envoy/ext_authz/ext_authz.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/ext_authz/ext_authz_impl.h"
#include "common/filter/ext_authz.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
ExtAuthzConfigFactory::createFilter(const envoy::api::v2::filter::network::ExtAuthz& proto_config,
                                    FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.grpc_service().has_envoy_grpc());
  ASSERT(!proto_config.grpc_service().envoy_grpc().cluster_name().empty());

  ExtAuthz::TcpFilter::ConfigSharedPtr ext_authz_config(new ExtAuthz::TcpFilter::Config(
      proto_config, context.scope(), context.runtime(), context.clusterManager()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 20);

  return [ grpc_service = proto_config.grpc_service(), &context, ext_authz_config,
           timeout_ms ](Network::FilterManager & filter_manager)
      ->void {

    auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(grpc_service,
                                                                                context.scope());

    auto client = std::make_unique<GrpcClientImpl>(async_client_factory->create(),
                                                   std::chrono::milliseconds(timeout_ms));
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new ExtAuthz::TcpFilter::Instance(
        ext_authz_config, std::move(client),
        timeoutclient_factory.create(std::chrono::milliseconds(timeout_ms)))});
  };
}

NetworkFilterFactoryCb ExtAuthzConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                  FactoryContext& context) {
  envoy::api::v2::filter::network::ExtAuthz proto_config;
  MessageUtil::loadFromJson(json_config.asJsonString(), proto_config);
  return createFilter(proto_config, context);
}

NetworkFilterFactoryCb
ExtAuthzConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                    FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<const envoy::api::v2::filter::network::ExtAuthz&>(
          proto_config),
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
