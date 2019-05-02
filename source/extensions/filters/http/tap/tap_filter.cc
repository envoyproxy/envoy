#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/http/tap/tap_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

FilterConfigImpl::FilterConfigImpl(
    const envoy::config::filter::http::tap::v2alpha::Tap& proto_config,
    const std::string& stats_prefix,
    Extensions::Common::Tap::TapConfigFactoryPtr&& config_factory,
     Server::Configuration::FactoryContext& context)
    : ExtensionConfigBase(proto_config.common_config(), std::move(config_factory), context.admin(),
                          context.singletonManager(), context.threadLocal(), context.dispatcher(),
                          stats_prefix,
                          context.scope(),
                          context.clusterManager(),
                          context.localInfo(),
                          context.random(),
                          context.api()
                          ),
      stats_(Filter::generateStats(stats_prefix, context.scope())) {}

HttpTapConfigSharedPtr FilterConfigImpl::currentConfig() {
  return currentConfigHelper<HttpTapConfig>();
}

FilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  // TODO(mattklein123): Consider whether we want to additionally namespace the stats on the
  // filter's configured opaque ID.
  std::string final_prefix = prefix + "tap.";
  return {ALL_TAP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  Upstream::ClusterInfoConstSharedPtr destination_cluster;
  if (tapper_ != nullptr) {
    auto&& route = decoder_callbacks_->route();
    if (route != nullptr) {
        const Router::RouteEntry *route_entry = route->routeEntry();
        if (route_entry != nullptr){
          auto&& cluster_name = route_entry->clusterName();
            if (!cluster_name.empty()) {
              auto *cluster = cluster_manager_.get(cluster_name);
              if (cluster != nullptr){
                destination_cluster = cluster->info();
              }
            }
        }
    }
    tapper_->onConnectionMetadataKnown(decoder_callbacks_->connection()->remoteAddress(), destination_cluster);
    tapper_->onRequestHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool) {
  if (tapper_ != nullptr) {
    tapper_->onRequestBody(data);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
  if (tapper_ != nullptr) {
    tapper_->onRequestTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onDestinationHostKnown(encoder_callbacks_->streamInfo().upstreamHost());
    tapper_->onResponseHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool) {
  if (tapper_ != nullptr) {
    tapper_->onResponseBody(data);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::HeaderMap& trailers) {
  if (tapper_ != nullptr) {
    tapper_->onResponseTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::log(const Http::HeaderMap*, const Http::HeaderMap*, const Http::HeaderMap*,
                 const StreamInfo::StreamInfo&) {
  if (tapper_ != nullptr && tapper_->onDestroyLog()) {
    config_->stats().rq_tapped_.inc();
  }
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
