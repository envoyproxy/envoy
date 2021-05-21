#include "extensions/filters/http/alternate_protocols_cache/filter.h"

#include "envoy/common/time.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "common/http/headers.h"

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

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (!config_->getAlternateProtocolCache()) {
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
      return Http::FilterHeadersStatus::Continue;
    }
    for (size_t i = 0; i < altsvc_vector.size(); ++i) {
      MonotonicTime expiration =
          config_->timeSource().monotonicTime() + std::chrono::seconds(altsvc_vector[i].max_age);
      Http::AlternateProtocolsCache::AlternateProtocol protocol(
          altsvc_vector[i].protocol_id, altsvc_vector[i].host, altsvc_vector[i].port, expiration);
      protocols.push_back(protocol);
    }
  }
  StreamInfo::StreamInfo& stream_info = encoder_callbacks_->streamInfo();
  const uint32_t port = stream_info.upstreamHost()->address()->ip()->port();
  const std::string& hostname = stream_info.upstreamHost()->hostname();
  Http::AlternateProtocolsCache::Origin origin(Http::Headers::get().SchemeValues.Https, hostname,
                                               port);
  config_->getAlternateProtocolCache()->setAlternatives(origin, protocols);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
