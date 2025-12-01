#pragma once

#include "envoy/extensions/filters/http/connection_pool_cardinality/v3/connection_pool_cardinality.pb.h"
#include "envoy/extensions/filters/http/connection_pool_cardinality/v3/connection_pool_cardinality.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

class ConnectionPoolCardinalityFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::connection_pool_cardinality::v3::
                                     ConnectionPoolCardinalityConfig> {
public:
  ConnectionPoolCardinalityFilterFactory()
      : FactoryBase("envoy.filters.http.connection_pool_cardinality") {}
  std::string name() const override { return "envoy.filters.http.connection_pool_cardinality"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::connection_pool_cardinality::v3::
                                ConnectionPoolCardinalityConfig>();
  }

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::connection_pool_cardinality::v3::
          ConnectionPoolCardinalityConfig& config,
      const std::string&, Server::Configuration::FactoryContext& context) override;
};

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
