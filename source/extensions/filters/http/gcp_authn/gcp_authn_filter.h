#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_client.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_client_impl.h"
#include "source/extensions/filters/http/gcp_authn/token_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

inline constexpr absl::string_view FilterName = "envoy.filters.http.gcp_authn";
inline const Envoy::Http::LowerCaseString& authorizationHeaderKey() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Http::LowerCaseString, "Authorization");
}
/**
 * All stats for the gcp authentication filter. @see stats_macros.h
 */
#define ALL_GCP_AUTHN_FILTER_STATS(COUNTER)                                                        \
  COUNTER(retrieve_audience_failed)                                                                \
  COUNTER(empty_audience)                                                                          \
  COUNTER(client_cert_fingerprint_calculated)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
struct GcpAuthnFilterStats {
  ALL_GCP_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using FilterConfigProto = envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

class FilterConfig {
public:
  FilterConfig(const FilterConfigProto& config,
               Server::Configuration::ServerFactoryContext& context,
               const std::string& stats_prefix, Stats::Scope& scope);

  const FilterConfigProto& config() const { return config_; }
  GcpAuthnFilterStats& stats() { return stats_; }
  Server::Configuration::ServerFactoryContext& context() const { return context_; }
  TokenCacheImpl* tokenCache() const {
    return token_cache_ ? &token_cache_->tls.get()->cache() : nullptr;
  }

private:
  const FilterConfigProto config_;
  Server::Configuration::ServerFactoryContext& context_;
  GcpAuthnFilterStats stats_;
  std::shared_ptr<TokenCache> token_cache_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

class GcpAuthnFilter : public Http::PassThroughDecoderFilter,
                       public GcpAuthnClient::Callbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  // State of this filter's communication with the external authentication service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };

  GcpAuthnFilter(FilterConfigSharedPtr filter_config, CertFingerprinterSharedPtr fingerprinter)
      : filter_config_(std::move(filter_config)), fingerprinter_(std::move(fingerprinter)),
        client_(std::make_unique<GcpAuthnClientImpl>(filter_config_->config(),
                                                     filter_config_->context())),
        jwt_token_cache_(filter_config_->tokenCache()) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;
  void onComplete(absl::StatusOr<GcpToken> token) override;

  State state() { return state_; }

  ~GcpAuthnFilter() override = default;

  // This is for testing only.
  const std::optional<std::string>& fingerprint() const { return client_cert_fingerprint_; }

private:
  friend class GcpAuthnFilterTest;

  std::optional<std::string> getClientCertFingerprint(Upstream::ThreadLocalCluster* cluster);

  GcpAuthnFilterStats generateStats(const std::string& stats_prefix, Stats::Scope& scope) {
    return {ALL_GCP_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix))};
  }

  FilterConfigSharedPtr filter_config_;
  const CertFingerprinterSharedPtr fingerprinter_;
  std::unique_ptr<GcpAuthnClient> client_;
  // The pointer to request headers for header manipulation later.
  Envoy::Http::RequestHeaderMap* request_header_map_ = nullptr;

  bool initiating_call_{};
  State state_{State::NotStarted};
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience_;
  std::optional<std::string> client_cert_fingerprint_;
  // This cache is optional (it will be nullptr if no cache configuration) and not owned by the
  // filter (thread local storage).
  TokenCacheImpl* jwt_token_cache_;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
