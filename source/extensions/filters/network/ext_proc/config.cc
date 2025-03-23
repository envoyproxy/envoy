#include "source/extensions/filters/network/ext_proc/config.h"

#include <chrono>
#include <string>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/ext_proc/v3/ext_proc.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

Network::FilterFactoryCb ExtProcConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ext_proc::v3::NetworkExternalProcessor& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr ext_proc_config =
      std::make_shared<Config>(proto_config, context.scope(), context.serverFactoryContext());

  THROW_IF_NOT_OK(Envoy::Config::Utility::checkTransportVersion(proto_config));
  return [ext_proc_config](Network::FilterManager& filter_manager) -> void {
    auto factory_or_error = context.serverFactoryContext()
                                .clusterManager()
                                .grpcAsyncClientManager()
                                .factoryForGrpcService(grpc_service, context.scope(), true);
    THROW_IF_NOT_OK_REF(factory_or_error.status());
    filter_manager.addFilter(std::make_shared<NetworkExtProcFilter>(ext_proc_config));
  };
}

/**
 * Static registration for the external authorization filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(ExtProcConfigFactory,
                        Server::Configuration::NamedNetworkFilterConfigFactory, "envoy.ext_proc");

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
