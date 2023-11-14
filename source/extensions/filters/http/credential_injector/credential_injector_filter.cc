#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

FilterConfig::FilterConfig(CredentialInjectorSharedPtr credential_injector, bool overwrite,
                           bool fail_request, const std::string& stats_prefix, Stats::Scope& scope)
    : injector_(credential_injector), overwrite_(overwrite), fail_request_(fail_request),
      stats_(generateStats(stats_prefix + "credential_injector.", scope)) {}

CredentialInjectorFilter::CredentialInjectorFilter(FilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus CredentialInjectorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  request_headers_ = &headers;

  in_flight_credential_request_ = config_->requestCredential(*this);

  // pause while we await the next step from the credential source, for example, an OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

void CredentialInjectorFilter::onSuccess() {
  decoder_callbacks_->dispatcher().post([this]() {
    bool success = config_->injectCredential(*request_headers_);

    if (success) {
      config_->stats().injected_.inc();
    } else {
      config_->stats().failed_.inc();
    }

    if (!success && config_.failRequest()) {
      decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                         nullptr, absl::nullopt, "failed_to_inject_credential");
      return;
    }

    decoder_callbacks_->continueDecoding();
  });
}

void CredentialInjectorFilter::onFailure() {
  config_->stats().failed_.inc();

  if (config_.failRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                       nullptr, absl::nullopt, "failed_to_get_credential");
    return;
  }

  decoder_callbacks_->continueDecoding();
}

void CredentialInjectorFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void CredentialInjectorFilter::onDestroy() {
  if (in_flight_credential_request_ != nullptr) {
    in_flight_credential_request_->cancel();
  }
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
