#include "extensions/filters/http/tap/tap_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

FilterConfigImpl::FilterConfigImpl(envoy::config::filter::http::tap::v2alpha::Tap proto_config,
                                   const std::string& stats_prefix,
                                   HttpTapConfigFactoryPtr&& config_factory, Stats::Scope& scope,
                                   Server::Admin& admin, Singleton::Manager& singleton_manager,
                                   ThreadLocal::SlotAllocator& tls,
                                   Event::Dispatcher& main_thread_dispatcher)
    : proto_config_(std::move(proto_config)), stats_(Filter::generateStats(stats_prefix, scope)),
      config_factory_(std::move(config_factory)), tls_slot_(tls.allocateSlot()) {

  // TODO(mattklein123): Admin is the only supported config type currently.
  ASSERT(proto_config_.has_admin_config());

  admin_handler_ = Extensions::Common::Tap::AdminHandler::getSingleton(admin, singleton_manager,
                                                                       main_thread_dispatcher);
  admin_handler_->registerConfig(*this, proto_config_.admin_config().config_id());
  ENVOY_LOG(debug, "initializing tap filter with admin endpoint (config_id={})",
            proto_config_.admin_config().config_id());

  tls_slot_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsFilterConfig>();
  });
}

FilterConfigImpl::~FilterConfigImpl() {
  if (admin_handler_) {
    admin_handler_->unregisterConfig(*this);
  }
}

HttpTapConfigSharedPtr FilterConfigImpl::currentConfig() {
  return tls_slot_->getTyped<TlsFilterConfig>().config_;
}

const std::string& FilterConfigImpl::adminId() {
  ASSERT(proto_config_.has_admin_config());
  return proto_config_.admin_config().config_id();
}

void FilterConfigImpl::clearTapConfig() {
  tls_slot_->runOnAllThreads([this] { tls_slot_->getTyped<TlsFilterConfig>().config_ = nullptr; });
}

void FilterConfigImpl::newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                    Common::Tap::Sink* admin_streamer) {
  HttpTapConfigSharedPtr new_config =
      config_factory_->createHttpConfigFromProto(std::move(proto_config), admin_streamer);
  tls_slot_->runOnAllThreads(
      [this, new_config] { tls_slot_->getTyped<TlsFilterConfig>().config_ = new_config; });
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

Http::FilterHeadersStatus Filter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (tapper_ != nullptr) {
    tapper_->onResponseHeaders(headers);
  }
  return Http::FilterHeadersStatus::Continue;
}

void Filter::log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                 const Http::HeaderMap*, const StreamInfo::StreamInfo&) {
  if (tapper_ != nullptr && tapper_->onDestroyLog(request_headers, response_headers)) {
    config_->stats().rq_tapped_.inc();
  }
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
