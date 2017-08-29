#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Http {

/**
 * A pass-through filter that talks to an external authn/authz service.
 *
 * When Envoy receives a request for which this filter is enabled, an
 * asynchronous request with the same HTTP method and headers, but an empty
 * body, is made to the configured external auth service. The original
 * request is stalled until the auth request completes.
 *
 * If the auth request returns HTTP 200, the original request is allowed
 * to continue. If any headers are listed in the extauth filter's "headers"
 * array, those headers will be copied from the auth response into the
 * original request (overwriting any duplicate headers).
 *
 * If the auth request returns anything other than HTTP 200, the original
 * request is rejected. The full response from the auth service is returned
 * as the response to the rejected request.
 *
 * Note that at present, a call to the external service is made for _every
 * request_ being routed.
 */

/**
 * All stats for the extauth filter. @see stats_macros.h
 */
// clang-format off
#define ALL_EXTAUTH_STATS(COUNTER)                                                                 \
  COUNTER(rq_failed)                                                                               \
  COUNTER(rq_passed)                                                                               \
  COUNTER(rq_rejected)                                                                             \
  COUNTER(rq_redirected)
// clang-format on

/**
 * Wrapper struct for extauth filter stats. @see stats_macros.h
 */
struct ExtAuthStats {
  ALL_EXTAUTH_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the extauth filter.
 */
struct ExtAuthConfig {
  Upstream::ClusterManager& cm_;
  ExtAuthStats stats_;
  std::string cluster_;
  std::chrono::milliseconds timeout_;
  std::vector<std::string> allowed_headers_;
  std::string path_prefix_;
};

typedef std::shared_ptr<const ExtAuthConfig> ExtAuthConfigConstSharedPtr;

/**
 * ExtAuth filter itself.
 */
class ExtAuth : Logger::Loggable<Logger::Id::filter>,
                public StreamDecoderFilter,
                public Http::AsyncClient::Callbacks {
public:
  ExtAuth(ExtAuthConfigConstSharedPtr config);
  ~ExtAuth();

  static ExtAuthStats generateStats(const std::string& prefix, Stats::Scope& scope);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

private:
  void dumpHeaders(const char* what, HeaderMap* headers);

  ExtAuthConfigConstSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  bool auth_complete_;
  Http::AsyncClient::Request* auth_request_{};
  Http::HeaderMap* request_headers_;
};

} // Http
} // namespace Envoy
