#include "source/extensions/filters/http/tap/tap_filter.h"

#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

FilterConfigImpl::FilterConfigImpl(
    const envoy::extensions::filters::http::tap::v3::Tap& proto_config,
    const std::string& stats_prefix, Common::Tap::TapConfigFactoryPtr&& config_factory,
    Stats::Scope& scope, OptRef<Server::Admin> admin, Singleton::Manager& singleton_manager,
    ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_thread_dispatcher)
    : ExtensionConfigBase(proto_config.common_config(), std::move(config_factory), admin,
                          singleton_manager, tls, main_thread_dispatcher),
      stats_(Filter::generateStats(stats_prefix, scope)) {}

HttpTapConfigSharedPtr FilterConfigImpl::currentConfig() {
  return currentConfigHelper<HttpTapConfig>();
}

FilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  // TODO(mattklein123): Consider whether we want to additionally namespace the stats on the
  // filter's configured opaque ID.
  std::string final_prefix = prefix + "tap.";
  return {ALL_TAP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onRequestHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool) {
  if ((tapper_ != nullptr) && (0 != data.length())) {
    tapper_->onRequestBody(data);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (tapper_ != nullptr) {
    tapper_->onRequestTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onResponseHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool) {
  if ((tapper_ != nullptr) && (0 != data.length())) {
    tapper_->onResponseBody(data);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (tapper_ != nullptr) {
    tapper_->onResponseTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::log(const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*,
                 const Http::ResponseTrailerMap*, const StreamInfo::StreamInfo&) {
  if (tapper_ != nullptr && tapper_->onDestroyLog()) {
    config_->stats().rq_tapped_.inc();
  }
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
