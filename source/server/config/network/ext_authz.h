#pragma once

#include <string>

#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the  external authorization filter. @see NamedNetworkFilterConfigFactory.
 */
class ExtAuthzConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                             FactoryContext& context) override;

  NetworkFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                      FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::network::ext_authz::v2::ExtAuthz()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().EXT_AUTHORIZATION; }

private:
  NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::ext_authz::v2::ExtAuthz& proto_config,
               FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
