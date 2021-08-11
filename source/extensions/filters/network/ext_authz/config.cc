#include "source/extensions/filters/network/ext_authz/config.h"

#include <chrono>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/network/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

Network::FilterFactoryCb ExtAuthzConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ext_authz::v3::ExtAuthz& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr ext_authz_config = std::make_shared<Config>(
      proto_config, context.scope(), context.getServerFactoryContext().bootstrap());
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 200);

  return [grpc_service = proto_config.grpc_service(), &context, ext_authz_config,
          transport_api_version = Envoy::Config::Utility::getAndCheckTransportVersion(proto_config),
          timeout_ms](Network::FilterManager& filter_manager) -> void {
    auto async_client_factory =
        context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
            grpc_service, context.scope(), true);

    auto client = std::make_unique<Filters::Common::ExtAuthz::GrpcClientImpl>(
        async_client_factory->createUncachedRawAsyncClient(), std::chrono::milliseconds(timeout_ms),
        transport_api_version);
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
        std::make_shared<Filter>(ext_authz_config, std::move(client))});
  };
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ExtAuthzConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.ext_authz"};

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
