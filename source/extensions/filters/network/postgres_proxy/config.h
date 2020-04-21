#pragma once

#include "envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/postgres_proxy/postgres_filter.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * Config registration for the Postgres proxy filter.
 */
class PostgresConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy> {
public:
  PostgresConfigFactory() : FactoryBase{NetworkFilterNames::get().Postgres} {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
