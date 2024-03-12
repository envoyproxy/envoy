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

  // If the credential already exists in the header and overwrite is false,
  // increase the counter and continue processing the request.
  if (absl::IsAlreadyExists(status)) {
    ASSERT(!overwrite_); // overwrite should be false if AlreadyExists is returned
    ENVOY_LOG(trace, "Credential already exists in the header");
    stats_.already_exists_.inc();
    // continue processing the request if credential already exists in the header
    return true;
  }

  // If failed to inject the credential
  if (!status.ok()) {
    ENVOY_LOG(debug, "Failed to inject credential: {}", status.message());
    stats_.failed_.inc();
    return allow_request_without_credential_;
  }

  // If successfully injected the credential
  ENVOY_LOG(debug, "Successfully injected credential");
  stats_.injected_.inc();
  return true;
}

CredentialInjectorFilter::CredentialInjectorFilter(FilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus CredentialInjectorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool) {
  // Initiate the credential provider if not already done.
  if (!credential_init_) {
    // Save the pointer to the request headers for header manipulation based on credential provider
    // response later.
    request_headers_ = &headers;

    in_flight_credential_request_ = config_->requestCredential(*this);

    // If the callback is called immediately, continue decoding.
    // We don't need to inject credential here because the credential has been injected in the
    // onSuccess callback.
    if (credential_init_) {
      return Http::FilterHeadersStatus::Continue;
    }

    // pause while we await the credential provider to retrieve the credential, for example, an
    // oauth2 credential provider may need to make a remote call to retrieve the credential.
    stop_iteration_ = true;
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  // The credential provider has failed to retrieve the credential
  if (!credential_success_) {
    config_->stats().failed_.inc();

    if (!config_->allowRequestWithoutCredential()) {
      decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                         nullptr, absl::nullopt, "failed_to_inject_credential");
      return Http::FilterHeadersStatus::StopIteration;
    }

    return Http::FilterHeadersStatus::Continue;
  }

  // The credential provider has successfully retrieved the credential, inject it to the header
  bool succeed = config_->injectCredential(*request_headers_);
  if (!succeed && !config_->allowRequestWithoutCredential()) {
    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                       nullptr, absl::nullopt, "failed_to_inject_credential");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

void CredentialInjectorFilter::onSuccess() {
  credential_init_ = true;
  credential_success_ = true;
  in_flight_credential_request_ = nullptr;

  assert(request_headers_ != nullptr);
  bool succeed = config_->injectCredential(*request_headers_);
  if (!succeed && !config_->allowRequestWithoutCredential()) {
    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                       nullptr, absl::nullopt, "failed_to_inject_credential");
    return;
  }

  // Only continue decoding if the callback is called from another thread.
  if (stop_iteration_) {
    stop_iteration_ = false;
    decoder_callbacks_->continueDecoding();
  }
}

void CredentialInjectorFilter::onFailure(const std::string& reason) {
  credential_init_ = true; // TODO: retry after a certain period of time
  credential_success_ = false;
  in_flight_credential_request_ = nullptr;

  // Credential provider has failed to retrieve the credential
  ENVOY_LOG(warn, "Failed to get credential: {}", reason);
  config_->stats().failed_.inc();

  if (!config_->allowRequestWithoutCredential()) {
    decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Failed to inject credential.",
                                       nullptr, absl::nullopt, "failed_to_inject_credential");
    return;
  }

  // Only continue decoding if the callback is called from another thread.
  if (stop_iteration_) {
    stop_iteration_ = false;
    decoder_callbacks_->continueDecoding();
  }
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
