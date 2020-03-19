#pragma once

#include "envoy/extensions/filters/network/postgresql_proxy/v3alpha/postgresql_proxy.pb.h"
#include "envoy/extensions/filters/network/postgresql_proxy/v3alpha/postgresql_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_filter.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

/**
 * Config registration for the PostgreSQL proxy filter.
 */
class PostgreSQLConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::postgresql_proxy::v3alpha::PostgreSQLProxy> {
public:
  PostgreSQLConfigFactory() : FactoryBase{NetworkFilterNames::get().PostgreSQL} {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::postgresql_proxy::v3alpha::PostgreSQLProxy&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
