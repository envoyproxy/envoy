#pragma once

#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.h"
#include "envoy/extensions/filters/http/orca_reporter/v3/orca_reporter.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

/**
 * Config registration for the ORCA reporter filter.
 */
class OrcaReporterFilterFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::orca_reporter::v3::OrcaReporter> {
public:
  OrcaReporterFilterFactory() : UnifiedFactoryBase("envoy.filters.http.orca_reporter") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryTyped(
      const envoy::extensions::filters::http::orca_reporter::v3::OrcaReporter& proto_config,
      Server::Configuration::GenericFactoryContext& context) override;
};

DECLARE_FACTORY(OrcaReporterFilterFactory);

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
