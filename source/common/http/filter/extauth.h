#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Http {

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
  std::string path_prefix_;
};

typedef std::shared_ptr<const ExtAuthConfig> ExtAuthConfigConstSharedPtr;

/**
 * A pass-through filter that talks to an external authn/authz service (or will soon...)
 */
class ExtAuth : Logger::Loggable<Logger::Id::filter>,
                public StreamDecoderFilter,
                public Http::AsyncClient::Callbacks {
public:
  ExtAuth(ExtAuthConfigConstSharedPtr config);
  ~ExtAuth();

  static ExtAuthStats generateStats(const std::string& prefix, Stats::Store& store);

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
  ExtAuthConfigConstSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  bool auth_complete_;
  Http::AsyncClient::Request* auth_request_{};
};

} // Http
} // Envoy
