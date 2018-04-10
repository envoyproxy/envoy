#pragma once

#include <string>

#include "envoy/config/filter/network/ext_authz/v2/ext_authz.pb.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

/**
 * Config registration for the  external authorization filter. @see NamedNetworkFilterConfigFactory.
 */
class ExtAuthzConfigFactory : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

  Server::Configuration::NetworkFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                               Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new envoy::config::filter::network::ext_authz::v2::ExtAuthz()};
  }

  std::string name() override { return Config::NetworkFilterNames::get().EXT_AUTHORIZATION; }

private:
  Server::Configuration::NetworkFilterFactoryCb
  createFilter(const envoy::config::filter::network::ext_authz::v2::ExtAuthz& proto_config,
               Server::Configuration::FactoryContext& context);
};

} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
