#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"

#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

FilterConfig::FilterConfig(CredentialInjectorSharedPtr credential_injector, bool overwrite,
                           bool allow_request_without_credential, const std::string& stats_prefix,
                           Stats::Scope& scope)
    : injector_(credential_injector), overwrite_(overwrite),
      allow_request_without_credential_(allow_request_without_credential),
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
    absl::Status status = config_->injectCredential(*request_headers_);

    // failed to inject the credential
    if (!status.ok()) {
      ENVOY_LOG(warn, "Failed to inject credential: {}", status.message());
      config_->stats().failed_.inc();

      if (config_->allowRequestWithoutCredential()) {
        decoder_callbacks_->continueDecoding();
        return;
      }

      decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                         nullptr, absl::nullopt, "failed_to_get_credential");
      return;
    }

    // Credential injected successfully
    config_->stats().injected_.inc();
    decoder_callbacks_->continueDecoding();
    return;
  });
}

void CredentialInjectorFilter::onFailure(const std::string& reason) {
  ENVOY_LOG(warn, "Failed to get credential: {}", reason);

  config_->stats().failed_.inc();

  if (config_->allowRequestWithoutCredential()) {
    decoder_callbacks_->continueDecoding();
    return;
  }

  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                     nullptr, absl::nullopt, "failed_to_get_credential");
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
