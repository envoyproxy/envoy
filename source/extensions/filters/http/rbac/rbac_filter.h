#pragma once

#include <memory>

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

/**
 * All stats for the RBAC filter. @see stats_macros.h
 */
// clang-format off
#define ALL_RBAC_FILTER_STATS(COUNTER)                                                             \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)
// clang-format on

/**
 * Wrapper struct for RBAC filter stats. @see stats_macros.h
 */
struct RBACFilterStats {
  ALL_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the RBAC filter.
 */
class RBACFilterConfig {
public:
  RBACFilterConfig(const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
                   const std::string& stats_prefix, Stats::Scope& scope);

  RBACFilterStats& stats() { return stats_; }
  const Filters::Common::RBAC::RBACEngine& engine(const Router::RouteConstSharedPtr) const;

private:
  RBACFilterStats stats_;
  const Filters::Common::RBAC::RBACEngineImpl engine_;
};

typedef std::shared_ptr<RBACFilterConfig> RBACFilterConfigSharedPtr;

/**
 * A filter that provides role-based access control authorization for HTTP requests.
 */
class RBACFilter : public Http::StreamDecoderFilter {
public:
  RBACFilter(RBACFilterConfigSharedPtr config) : config_(config) {}

  static RBACFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override { stream_destroyed_ = true; }

private:
  RBACFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool stream_destroyed_{};
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
