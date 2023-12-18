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

// Inject configured credential to the HTTP request header.
// return true if successful
bool FilterConfig::injectCredential(Http::RequestHeaderMap& headers) {
  absl::Status status = injector_->inject(headers, overwrite_);

  // credential already exists in the header and overwrite is false
  if (absl::IsAlreadyExists(status)) {
    ASSERT(!overwrite_); // overwrite should be false if AlreadyExists is returned
    ENVOY_LOG(debug, "Credential already exists in the header");
    stats_.already_exists_.inc();
    // continue processing the request if credential already exists in the header
    return true;
  }

  // failed to inject the credential
  if (!status.ok()) {
    ENVOY_LOG(debug, "Failed to inject credential: {}", status.message());
    stats_.failed_.inc();
    return allow_request_without_credential_;
  }

  // successfully injected the credential
  ENVOY_LOG(debug, "Successfully injected credential");
  stats_.injected_.inc();
  return true;
}

CredentialInjectorFilter::CredentialInjectorFilter(FilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus CredentialInjectorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool) {
  request_headers_ = &headers;
  in_flight_credential_request_ = config_->requestCredential(*this);

  // pause while we await the next step from the credential provider, for example, an OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

void CredentialInjectorFilter::onSuccess() {
  // Since onSuccess is called by the credential provider in other threads than the event
  // dispatcher, we need to post the injection to the event dispatcher thread.
  decoder_callbacks_->dispatcher().post([this]() {
    bool succeed = config_->injectCredential(*request_headers_);

    if (!succeed) {
      decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                         nullptr, absl::nullopt, "failed_to_inject_credential");
    }

    decoder_callbacks_->continueDecoding();
    return;
  });
}

void CredentialInjectorFilter::onFailure(const std::string& reason) {
  ENVOY_LOG(warn, "Failed to get credential: {}", reason);

  // Since onFailure is called by the credential provider in other threads than the event
  // dispatcher, we need to post the handling to the event dispatcher thread.
  decoder_callbacks_->dispatcher().post([this]() {
    config_->stats().failed_.inc();

    if (config_->allowRequestWithoutCredential()) {
      decoder_callbacks_->continueDecoding();
      return;
    }

    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                       nullptr, absl::nullopt, "failed_to_inject_credential");
    return;
  });
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
