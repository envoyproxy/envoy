#include "server/http/health_check.h"

#include <chrono>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the health check filter. @see NamedHttpFilterConfigFactory.
 */
HttpFilterFactoryCb HealthCheckFilterConfig::createFilterFactory(const Json::Object& config,
                                                                 const std::string&,
                                                                 FactoryContext& context) {
  config.validateSchema(Json::Schema::HEALTH_CHECK_HTTP_FILTER_SCHEMA);

  bool pass_through_mode = config.getBoolean("pass_through_mode");
  int64_t cache_time_ms = config.getInteger("cache_time_ms", 0);
  std::string hc_endpoint = config.getString("endpoint");

  if (!pass_through_mode && cache_time_ms) {
    throw EnvoyException("cache_time_ms must not be set when path_through_mode is disabled");
  }

  HealthCheckCacheManagerSharedPtr cache_manager;
  if (cache_time_ms > 0) {
    cache_manager.reset(new HealthCheckCacheManager(context.dispatcher(),
                                                    std::chrono::milliseconds(cache_time_ms)));
  }

  return [&context, pass_through_mode, cache_manager,
          hc_endpoint](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
        new HealthCheckFilter(context, pass_through_mode, cache_manager, hc_endpoint)});
  };
}

/**
 * Static registration for the health check filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<HealthCheckFilterConfig, NamedHttpFilterConfigFactory> register_;

} // namespace Configuration
} // namespace Server

HealthCheckCacheManager::HealthCheckCacheManager(Event::Dispatcher& dispatcher,
                                                 std::chrono::milliseconds timeout)
    : clear_cache_timer_(dispatcher.createTimer([this]() -> void { onTimer(); })),
      timeout_(timeout) {
  onTimer();
}

void HealthCheckCacheManager::onTimer() {
  use_cached_response_code_ = false;
  clear_cache_timer_->enableTimer(timeout_);
}

Http::FilterHeadersStatus HealthCheckFilter::decodeHeaders(Http::HeaderMap& headers,
                                                           bool end_stream) {
  if (headers.Path()->value() == endpoint_.c_str()) {
    health_check_request_ = true;
    callbacks_->requestInfo().healthCheck(true);

    // If we are not in pass through mode, we always handle. Otherwise, we handle if the server is
    // in the failed state or if we are using caching and we should use the cached response.
    if (!pass_through_mode_ || context_.healthCheckFailed() ||
        (cache_manager_ && cache_manager_->useCachedResponseCode())) {
      handling_ = true;
    }
  }

  if (end_stream && handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterHeadersStatus::StopIteration : Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus HealthCheckFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream && handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterDataStatus::StopIterationNoBuffer
                   : Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus HealthCheckFilter::decodeTrailers(Http::HeaderMap&) {
  if (handling_) {
    onComplete();
  }

  return handling_ ? Http::FilterTrailersStatus::StopIteration
                   : Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus HealthCheckFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (health_check_request_) {
    if (cache_manager_) {
      cache_manager_->setCachedResponseCode(
          static_cast<Http::Code>(Http::Utility::getResponseStatus(headers)));
    }

    headers.insertEnvoyUpstreamHealthCheckedCluster().value(context_.localInfo().clusterName());
  } else if (context_.healthCheckFailed()) {
    headers.insertEnvoyImmediateHealthCheckFail().value(
        Http::Headers::get().EnvoyImmediateHealthCheckFailValues.True);
  }

  return Http::FilterHeadersStatus::Continue;
}

void HealthCheckFilter::onComplete() {
  ASSERT(handling_);
  Http::HeaderMapPtr headers;
  if (context_.healthCheckFailed()) {
    callbacks_->requestInfo().setResponseFlag(
        Http::AccessLog::ResponseFlag::FailedLocalHealthCheck);
    headers.reset(new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::ServiceUnavailable))}});
  } else {
    Http::Code final_status = Http::Code::OK;
    if (cache_manager_) {
      final_status = cache_manager_->getCachedResponseCode();
    }

    if (!Http::CodeUtility::is2xx(enumToInt(final_status))) {
      callbacks_->requestInfo().setResponseFlag(
          Http::AccessLog::ResponseFlag::FailedLocalHealthCheck);
    }

    headers.reset(new Http::HeaderMapImpl{
        {Http::Headers::get().Status, std::to_string(enumToInt(final_status))}});
  }

  callbacks_->encodeHeaders(std::move(headers), true);
}

} // namespace Envoy
