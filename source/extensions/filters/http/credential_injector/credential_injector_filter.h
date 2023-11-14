#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/injected_credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

/**
 * All Credential Injector filter stats. @see stats_macros.h
 */
#define ALL_credential_injector_STATS(COUNTER)                                                     \
  COUNTER(injected)                                                                                \
  COUNTER(failed)

/**
 * Struct definition for Credential Injector stats. @see stats_macros.h
 */
struct CredentialInjectorStats {
  ALL_credential_injector_STATS(GENERATE_COUNTER_STRUCT)
};

using Envoy::Extensions::Credentials::Common::CredentialInjector;
using Envoy::Extensions::Credentials::Common::CredentialInjectorSharedPtr;

/**
 * Configuration for the Credential Injector filter.
 */
class FilterConfig {
public:
  FilterConfig(CredentialInjectorSharedPtr, bool overwrite, bool fail_request,
               const std::string& stats_prefix, Stats::Scope& scope);
  CredentialInjectorStats& stats() { return stats_; }

  CredentialInjector::RequestPtr requestCredential(CredentialInjector::Callbacks& callbacks) {
    return injector_->requestCredential(callbacks);
  }

  // Inject configured credential to the HTTP request header.
  // Return true if the credential has been successful injected into the header.
  bool injectCredential(Http::RequestHeaderMap& headers) {
    return injector_->inject(headers, overwrite_);
  }

  bool failRequest() const { return fail_request_; }

private:
  static CredentialInjectorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CredentialInjectorStats{
        ALL_credential_injector_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CredentialInjectorSharedPtr injector_;
  bool overwrite_;
  bool fail_request_;
  CredentialInjectorStats stats_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// The Envoy filter to inject credentials.
class CredentialInjectorFilter : public Http::PassThroughFilter,
                                 public CredentialInjector::Callbacks,
                                 public Logger::Loggable<Logger::Id::credential_injector> {
public:
  CredentialInjectorFilter(FilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  // CredentialInjector::Callbacks
  void onSuccess() override;
  void onFailure() override;

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::RequestHeaderMap* request_headers_{};

  FilterConfigSharedPtr config_;

  // Tracks any outstanding in-flight credential requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  CredentialInjector::RequestPtr in_flight_credential_request_;
};

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
