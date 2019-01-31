#include "extensions/filters/http/tap/tap_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

FilterConfigImpl::FilterConfigImpl(
    const envoy::config::filter::http::tap::v2alpha::Tap& proto_config,
    const std::string& stats_prefix, Common::Tap::TapConfigFactoryPtr&& config_factory,
    Stats::Scope& scope, Server::Admin& admin, Singleton::Manager& singleton_manager,
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

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onRequestHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap& trailers) {
  // TODO(mattklein123): Why is this not provided in the log callback? Do a follow-up to make it so.
  request_trailers_ = &trailers;
  if (tapper_ != nullptr) {
    tapper_->onRequestTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onResponseHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::HeaderMap& trailers) {
  if (tapper_ != nullptr) {
    tapper_->onResponseTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                 const Http::HeaderMap* response_trailers, const StreamInfo::StreamInfo&) {
  if (tapper_ != nullptr && tapper_->onDestroyLog(request_headers, request_trailers_,
                                                  response_headers, response_trailers)) {
    config_->stats().rq_tapped_.inc();
  }
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
