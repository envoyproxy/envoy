#pragma once

#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/http_server_properties_cache_manager_impl.h"
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
      Http::HttpServerPropertiesCacheManagerFactory& alternate_protocol_cache_manager_factory,
      TimeSource& time_source);

  // Returns the alternate protocols cache for the current thread.
  Http::HttpServerPropertiesCacheSharedPtr getAlternateProtocolCache(Event::Dispatcher& dispatcher);

  TimeSource& timeSource() { return time_source_; }
  const Http::HttpServerPropertiesCacheManagerSharedPtr alternateProtocolCacheManager() {
    return alternate_protocol_cache_manager_;
  }

private:
  const Http::HttpServerPropertiesCacheManagerSharedPtr alternate_protocol_cache_manager_;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config_;
  TimeSource& time_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Alternate protocol cache filter which parses the alt-svc response header and updates
 * the cache accordingly.
 */
class Filter : public Http::PassThroughEncoderFilter,
               Logger::Loggable<Logger::Id::alternate_protocols_cache> {
public:
  Filter(const FilterConfigSharedPtr& config, Event::Dispatcher& thread_local_dispatcher);

  // Http::PassThroughEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& header,
                                          bool end_stream) override;
  void onDestroy() override;

private:
  FilterConfigSharedPtr config_;
  Event::Dispatcher& dispatcher_;
};

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
