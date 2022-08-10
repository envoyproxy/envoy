#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_impl.h"
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
#define ALL_GCP_AUTHN_FILTER_STATS(COUNTER) COUNTER(retrieve_audience_failed)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
struct GcpAuthnFilterStats {
  ALL_GCP_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using FilterConfigSharedPtr =
    std::shared_ptr<const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>;

class GcpAuthnFilter : public Http::PassThroughFilter,
                       public RequestCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  // State of this filter's communication with the external authentication service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };

  GcpAuthnFilter(FilterConfigSharedPtr filter_config,
                 Server::Configuration::FactoryContext& context, const std::string& stats_prefix,
                 TokenCacheImpl<JwtToken>* token_cache)
      : filter_config_(std::move(filter_config)), context_(context),
        client_(std::make_unique<GcpAuthnClient>(*filter_config_, context_)),
        stats_(generateStats(stats_prefix, context_.scope())), jwt_token_cache_(token_cache) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;
  void onComplete(const Http::ResponseMessage* response_ptr) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  State state() { return state_; }
  GcpAuthnFilterStats& stats() { return stats_; }

  ~GcpAuthnFilter() override = default;

private:
  GcpAuthnFilterStats generateStats(const std::string& stats_prefix, Stats::Scope& scope) {
    return {ALL_GCP_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix))};
  }

  FilterConfigSharedPtr filter_config_;
  Server::Configuration::FactoryContext& context_;
  std::unique_ptr<GcpAuthnClient> client_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  // The pointer to request headers for header manipulation later.
  Envoy::Http::RequestHeaderMap* request_header_map_ = nullptr;

  GcpAuthnFilterStats stats_;

  bool initiating_call_{};
  State state_{State::NotStarted};
  std::string audience_str_;
  // This cache is optional (it will be nullptr if no cache configuration) and not owned by the
  // filter (thread local storage).
  TokenCacheImpl<JwtToken>* jwt_token_cache_;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
