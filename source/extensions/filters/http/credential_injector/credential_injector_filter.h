#pragma once

#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/credentials/common/credential.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

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

using Envoy::Extensions::Credentials::Common::CredentialInjectorSharedPtr;

/**
 * Configuration for the Credential Injector filter.
 */
class FilterConfig {
public:
  FilterConfig(CredentialInjectorSharedPtr, bool overrite, const std::string& stats_prefix,
               Stats::Scope& scope);
  CredentialInjectorStats& stats() { return stats_; }

  // Inject configured credential to the HTTP request header.
  // Return true if the credential has been successful injected into the header.
  bool injectCredential(Http::RequestHeaderMap& headers);

private:
  static CredentialInjectorStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CredentialInjectorStats{
        ALL_credential_injector_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CredentialInjectorSharedPtr injector_;
  bool overrite_;
  CredentialInjectorStats stats_;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

// The Envoy filter to inject credentials.
class CredentialInjectorFilter : public Http::PassThroughFilter,
                                 public Logger::Loggable<Logger::Id::credential_injector> {
public:
  CredentialInjectorFilter(FilterConfigSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  FilterConfigSharedPtr config_;
};

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
