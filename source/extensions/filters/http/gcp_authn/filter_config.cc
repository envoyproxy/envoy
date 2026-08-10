#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include <memory>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

absl::StatusOr<Http::FilterFactoryCb> GcpAuthnFilterFactory::createFilterFactory(
    const GcpAuthnFilterConfig& config, const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope) {

  // config.retry_policy has an invalid case that could not be validated by the
  // proto validation annotation. It has to be validated by the code.
  if (config.has_retry_policy()) {
    RETURN_IF_NOT_OK(Http::Utility::validateCoreRetryPolicy(config.retry_policy()));
  }

  FilterConfigSharedPtr filter_config =
      std::make_shared<FilterConfig>(config, context, stats_prefix, scope);
  auto fingerprinter = std::make_shared<CertFingerprinterImpl>();

  return [filter_config, fingerprinter](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<GcpAuthnFilter>(filter_config, fingerprinter));
  };
}

absl::StatusOr<Http::FilterFactoryCb> GcpAuthnFilterFactory::createFilterFactoryFromProtoTyped(
    const GcpAuthnFilterConfig& config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  return createFilterFactory(config, stats_prefix, context.serverFactoryContext(), context.scope());
}

absl::StatusOr<Http::FilterFactoryCb> GcpAuthnFilterFactory::createHttpFilterFactoryFromProtoTyped(
    const GcpAuthnFilterConfig& config, const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  return createFilterFactory(config, stats_prefix, context, context.scope());
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GcpAuthnFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
