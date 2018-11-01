#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ForwardOriginalSni {

/**
 * Config registration for the original_sni filter. @see NamedNetworkFilterConfigFactory.
 */
class ForwardOriginalSniNetworkFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Server::Configuration::FactoryContext&) override;
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() override { return NetworkFilterNames::get().ForwardOriginalSni; }
};

} // namespace ForwardOriginalSni
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
