#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for http filters that have empty configuration blocks.
 * The boiler plate instantation functions (createFilterFactory, createFilterFactoryFromProto,
 * and createEmptyConfigProto) are implemented here. Users of this class have to implement
 * the createFilter function that instantiates the actual filter.
 */
class EmptyHttpFilterConfig : public NamedHttpFilterConfigFactory {
public:
  virtual HttpFilterFactoryCb createFilter(const std::string& stat_prefix,
                                           FactoryContext& context) PURE;

  HttpFilterFactoryCb createFilterFactory(const Json::Object&, const std::string& stat_prefix,
                                          FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                   const std::string& stat_prefix,
                                                   FactoryContext& context) override {
    return createFilter(stat_prefix, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
