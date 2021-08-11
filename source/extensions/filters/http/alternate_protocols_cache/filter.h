#pragma once

#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/alternate_protocols_cache_manager_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

/**
 * Configuration for the alternate protocol cache filter.
 */
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig&
          proto_config,
      Http::AlternateProtocolsCacheManagerFactory& alternate_protocol_cache_manager_factory,
      TimeSource& time_source);

  // Returns the alternate protocols cache for the current thread.
  Http::AlternateProtocolsCacheSharedPtr getAlternateProtocolCache();

  TimeSource& timeSource() { return time_source_; }

private:
  const Http::AlternateProtocolsCacheManagerSharedPtr alternate_protocol_cache_manager_;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config_;
  TimeSource& time_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Alternate protocol cache filter which parses the alt-svc response header and updates
 * the cache accordingly.
 */
class Filter : public Http::PassThroughEncoderFilter, Logger::Loggable<Logger::Id::forward_proxy> {
public:
  explicit Filter(const FilterConfigSharedPtr& config);

  // Http::PassThroughEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& header,
                                          bool end_stream) override;
  void onDestroy() override;

private:
  const Http::AlternateProtocolsCacheSharedPtr cache_;
  TimeSource& time_source_;
};

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
