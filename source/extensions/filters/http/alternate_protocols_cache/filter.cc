#include "source/extensions/filters/http/alternate_protocols_cache/filter.h"

#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/http_server_properties_cache_impl.h"
#include "source/common/http/http_server_properties_cache_manager_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig& config,
    Http::HttpServerPropertiesCacheManager& cache_manager, TimeSource& time_source)
    : alternate_protocol_cache_manager_(cache_manager), time_source_(time_source) {
  if (config.has_alternate_protocols_cache_options()) {
    ENVOY_LOG_MISC(warn, "Using deprecated and ignored alternate_protocols_cache_options in "
                         "alternate_protocols_cache config.");
  }
}

void Filter::onDestroy() {}

Filter::Filter(const FilterConfigSharedPtr& config, Event::Dispatcher& dispatcher)
    : config_(config), dispatcher_(dispatcher) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  const auto alt_svc = headers.get(Http::CustomHeaders::get().AltSvc);
  if (alt_svc.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::HttpServerPropertiesCacheSharedPtr cache;
  auto info = encoder_callbacks_->streamInfo().upstreamClusterInfo();
  if (info && (*info)->alternateProtocolsCacheOptions()) {
    cache = config_->alternateProtocolCacheManager().getCache(
        *((*info)->alternateProtocolsCacheOptions()), dispatcher_);
  }
  if (!cache) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::vector<Http::HttpServerPropertiesCache::AlternateProtocol> protocols;
  for (size_t i = 0; i < alt_svc.size(); ++i) {
    std::vector<Http::HttpServerPropertiesCache::AlternateProtocol> advertised_protocols =
        Http::HttpServerPropertiesCacheImpl::alternateProtocolsFromString(
            alt_svc[i]->value().getStringView(), config_->timeSource(), false);
    if (advertised_protocols.empty()) {
      ENVOY_LOG(trace, "Invalid Alt-Svc header received: '{}'",
                alt_svc[i]->value().getStringView());
      return Http::FilterHeadersStatus::Continue;
    }
    protocols.insert(protocols.end(), std::make_move_iterator(advertised_protocols.begin()),
                     std::make_move_iterator(advertised_protocols.end()));
  }

  // The upstream host is used here, instead of the :authority request header because
  // Envoy routes request to upstream hosts not to origin servers directly. This choice would
  // allow HTTP/3 to be used on a per-upstream host basis, even for origins which are load
  // balanced across them.
  Upstream::HostDescriptionConstSharedPtr host =
      encoder_callbacks_->streamInfo().upstreamInfo()->upstreamHost();
  absl::string_view hostname = host->hostname();
  if (encoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection() &&
      !encoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()->sni().empty()) {
    // In the case the configured hostname and SNI differ, prefer SNI where
    // available.
    hostname = encoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()->sni();
  }
  auto host_addr = host->address();
  const uint32_t port = (host_addr ? host_addr->ip()->port() : 443);
  Http::HttpServerPropertiesCache::Origin origin(Http::Headers::get().SchemeValues.Https, hostname,
                                                 port);
  cache->setAlternatives(origin, protocols);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
