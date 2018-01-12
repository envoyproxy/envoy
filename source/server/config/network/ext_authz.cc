#include "server/config/network/ext_authz.h"

#include <chrono>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "envoy/ext_authz/ext_authz.h"

#include "common/config/filter_json.h"
#include "common/ext_authz/ext_authz_impl.h"
#include "common/filter/ext_authz.h"
#include "common/protobuf/utility.h"

#include "api/filter/network/ext_authz.pb.validate.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
ExtAuthzConfigFactory::createFilter(const envoy::api::v2::filter::network::ExtAuthz& proto_config,
                                 FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(proto_config.grpc_service().has_envoy_grpc());
  ASSERT(!proto_config.grpc_service().envoy_grpc().cluster_name().empty());

  ExtAuthz::TcpFilter::ConfigSharedPtr ext_authz_config(
      new ExtAuthz::TcpFilter::Config(proto_config, context.scope(), context.runtime(), context.clusterManager()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config.grpc_service(), timeout, 20);

/* @saumoh: could we just create one client_factory and use it the lambda?
  ExtAuthz::ClientFactoryPtr client_factory(
      new ExtAuthz::GrpcFactoryImpl(ext_authz_config->cluster(), ext_authz_config->cm()));
*/

  return [ext_authz_config, timeout_ms](Network::FilterManager& filter_manager) -> void {
    ExtAuthz::GrpcFactoryImpl client_factory(ext_authz_config->cluster(), ext_authz_config->cm());

    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{
      new ExtAuthz::TcpFilter::Instance(ext_authz_config, client_factory.create(std::chrono::milliseconds(timeout_ms)))});
  };
}

NetworkFilterFactoryCb ExtAuthzConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                               FactoryContext& context) {
  envoy::api::v2::filter::network::ExtAuthz proto_config;
  Config::FilterJson::translateTcpExtAuthzFilter(json_config, proto_config);
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
