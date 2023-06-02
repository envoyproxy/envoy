#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

FilterConfig::FilterConfig(CredentialInjectorSharedPtr credential_injector, bool overrite,
                           const std::string& stats_prefix, Stats::Scope& scope)
    : injector_(credential_injector), overrite_(overrite),
      stats_(generateStats(stats_prefix + "credential_injector.", scope)) {}

bool FilterConfig::injectCredential(Http::RequestHeaderMap& headers) {
  return injector_->inject(headers, overrite_);
}

CredentialInjectorFilter::CredentialInjectorFilter(FilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus CredentialInjectorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  if (config_->injectCredential(headers)) {
    config_->stats().injected_.inc();
  } else {
    config_->stats().failed_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
