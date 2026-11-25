#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha/postgres_proxy.pb.validate.h"
#include "contrib/postgres_proxy/filters/network/source/postgres_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

/**
 * Config registration for the Postgres proxy filter.
 */
class PostgresConfigFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy>,
      Logger::Loggable<Logger::Id::filter> {
public:
  PostgresConfigFactory() : ExceptionFreeFactoryBase{NetworkFilterNames::get().PostgresProxy} {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
