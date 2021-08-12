#include "source/extensions/filters/http/alternate_protocols_cache/filter.h"

#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "source/common/http/headers.h"

#include "quiche/spdy/core/spdy_alt_svc_wire_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

using CustomClusterType = envoy::config::cluster::v3::Cluster::CustomClusterType;

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig&
        proto_config,
    Http::AlternateProtocolsCacheManagerFactory& alternate_protocol_cache_manager_factory,
    TimeSource& time_source)
    : alternate_protocol_cache_manager_(alternate_protocol_cache_manager_factory.get()),
      proto_config_(proto_config), time_source_(time_source) {}

Http::AlternateProtocolsCacheSharedPtr FilterConfig::getAlternateProtocolCache() {
  return proto_config_.has_alternate_protocols_cache_options()
             ? alternate_protocol_cache_manager_->getCache(
                   proto_config_.alternate_protocols_cache_options())
             : nullptr;
}

void Filter::onDestroy() {}

Filter::Filter(const FilterConfigSharedPtr& config)
    : cache_(config->getAlternateProtocolCache()), time_source_(config->timeSource()) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (!cache_) {
    return Http::FilterHeadersStatus::Continue;
  }
  const auto alt_svc = headers.get(Http::CustomHeaders::get().AltSvc);
  if (alt_svc.empty()) {
    return Http::FilterHeadersStatus::Continue;
  }
  std::vector<Http::AlternateProtocolsCache::AlternateProtocol> protocols;
  for (size_t i = 0; i < alt_svc.size(); ++i) {
    spdy::SpdyAltSvcWireFormat::AlternativeServiceVector altsvc_vector;
    if (!spdy::SpdyAltSvcWireFormat::ParseHeaderFieldValue(alt_svc[i]->value().getStringView(),
                                                           &altsvc_vector)) {
      ENVOY_LOG(trace, "Invalid Alt-Svc header received: '{}'",
                alt_svc[i]->value().getStringView());
      return Http::FilterHeadersStatus::Continue;
    }
    for (const auto& alt_svc : altsvc_vector) {
      MonotonicTime expiration =
          time_source_.monotonicTime() + std::chrono::seconds(alt_svc.max_age);
      Http::AlternateProtocolsCache::AlternateProtocol protocol(alt_svc.protocol_id, alt_svc.host,
                                                                alt_svc.port, expiration);
      protocols.push_back(protocol);
    }
  }
  // The upstream host is used here, instead of the :authority request header because
  // Envoy routes request to upstream hosts not to origin servers directly. This choice would
  // allow HTTP/3 to be used on a per-upstream host basis, even for origins which are load
  // balanced across them.
  Upstream::HostDescriptionConstSharedPtr host = encoder_callbacks_->streamInfo().upstreamHost();
  const uint32_t port = host->address()->ip()->port();
  const std::string& hostname = host->hostname();
  Http::AlternateProtocolsCache::Origin origin(Http::Headers::get().SchemeValues.Https, hostname,
                                               port);
  cache_->setAlternatives(origin, protocols);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
