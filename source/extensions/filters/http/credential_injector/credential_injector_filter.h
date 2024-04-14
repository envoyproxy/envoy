#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/http/injected_credentials/common/credential.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

/**
 * All Credential Injector filter stats. @see stats_macros.h
 */
#define ALL_CREDENTIAL_INJECTOR_STATS(COUNTER)                                                     \
  COUNTER(injected)                                                                                \
  COUNTER(failed)                                                                                  \
  COUNTER(already_exists)

/**
 * Struct definition for Credential Injector stats. @see stats_macros.h
 */
struct CredentialInjectorStats {
  ALL_CREDENTIAL_INJECTOR_STATS(GENERATE_COUNTER_STRUCT)
};

using Envoy::Extensions::Http::InjectedCredentials::Common::CredentialInjector;
using Envoy::Extensions::Http::InjectedCredentials::Common::CredentialInjectorSharedPtr;

/**
 * Configuration for the Credential Injector filter.
 */
class FilterConfig : public Logger::Loggable<Logger::Id::credential_injector> {
public:
  FilterConfig(CredentialInjectorSharedPtr, bool overwrite, bool allow_request_without_credential,
               const std::string& stats_prefix, Stats::Scope& scope);
  CredentialInjectorStats& stats() { return stats_; }

  // Inject configured credential to the HTTP request header.
  // return should continue processing the request or not
  bool injectCredential(Envoy::Http::RequestHeaderMap& headers);

  bool allowRequestWithoutCredential() const { return allow_request_without_credential_; }

private:
  static CredentialInjectorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CredentialInjectorStats{
        ALL_CREDENTIAL_INJECTOR_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CredentialInjectorSharedPtr injector_;
  const bool overwrite_;
  const bool allow_request_without_credential_;
  CredentialInjectorStats stats_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// The Envoy filter to inject credentials.
class CredentialInjectorFilter : public Envoy::Http::PassThroughDecoderFilter,
                                 public Logger::Loggable<Logger::Id::credential_injector> {
public:
  CredentialInjectorFilter(FilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool) override;

private:
  FilterConfigSharedPtr config_;
};

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
