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
      stats_(generateStats(stats_prefix, scope)) {}

// Inject configured credential to the HTTP request header.
// return true if successful
bool FilterConfig::injectCredential(Envoy::Http::RequestHeaderMap& headers) {
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

Envoy::Http::FilterHeadersStatus
CredentialInjectorFilter::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  bool succeed = config_->injectCredential(headers);
  if (!succeed) {
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::Unauthorized,
                                       "Failed to inject credential.", nullptr, absl::nullopt,
                                       "failed_to_inject_credential");
    return Envoy::Http::FilterHeadersStatus::StopIteration;
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
