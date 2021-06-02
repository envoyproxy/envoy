#pragma once

#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/alternate_protocols_cache_manager_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig&
          proto_config,
      Http::AlternateProtocolsCacheManagerFactory& alternate_protocol_cache_manager_factory,
      TimeSource& time_source);

  Http::AlternateProtocolsCacheSharedPtr getAlternateProtocolCache();

  TimeSource& timeSource() { return time_source_; }

private:
  const Http::AlternateProtocolsCacheManagerSharedPtr alternate_protocol_cache_manager_;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config_;
  TimeSource& time_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

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
