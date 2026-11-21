#include "source/extensions/filters/http/orca_reporter/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/orca_reporter/orca_reporter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {

absl::StatusOr<Http::FilterFactoryCb> OrcaReporterFilterFactory::createFilterFactoryTyped(
    const envoy::extensions::filters::http::orca_reporter::v3::OrcaReporter& proto_config,
    Server::Configuration::GenericFactoryContext& context) {
  absl::StatusOr<OrcaReporterConfigSharedPtr> config =
      OrcaReporterConfig::create(proto_config, context.serverFactoryContext());
  RETURN_IF_NOT_OK_REF(config.status());
  return [config = std::move(config).value()](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamEncoderFilter(std::make_shared<OrcaReporterFilter>(config));
  };
}

/**
 * Static registration for the ORCA reporter filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OrcaReporterFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
