#include "extensions/filters/http/buffer/buffer_filter.h"

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

BufferFilter::BufferFilter(BufferFilterConfigConstSharedPtr config) : config_(config) {}

BufferFilter::~BufferFilter() { ASSERT(!request_timeout_); }

void BufferFilter::initConfig() {
  ASSERT(!config_inited_);
  config_inited_ = true;

  if (!callbacks_->route() || !callbacks_->route()->routeEntry()) {
    return;
  }

  const std::string& name = HttpFilterNames::get().BUFFER;
  const auto* entry = callbacks_->route()->routeEntry();

  const Protobuf::Message* raw_cfg =
      entry->perFilterConfig(name) ?: entry->virtualHost().perFilterConfig(name);

  if (!raw_cfg) {
    // No route-local config is specified.
    return;
  }

  const auto* cfg =
      dynamic_cast<const envoy::config::filter::http::buffer::v2::BufferPerRoute*>(raw_cfg);

  if (cfg->disabled()) {
    // Filter is disabled.
    disabled_ = true;
    return;
  }
  if (!cfg->has_buffer()) {
    // OneOf for the route-local config was empty. Should this be an ASSERT_RELEASE?
    return;
  }

  const auto& buf = cfg->buffer();
  auto stats = config_->stats_;

  config_.reset(new BufferFilterConfig{
      stats, static_cast<uint64_t>(buf.max_request_bytes().value()),
      std::chrono::seconds(PROTOBUF_GET_SECONDS_REQUIRED(buf, max_request_time))});
}

Http::FilterHeadersStatus BufferFilter::decodeHeaders(Http::HeaderMap&, bool end_stream) {
  if (end_stream) {
    // If this is a header-only request, we don't need to do any buffering.
    return Http::FilterHeadersStatus::Continue;
  }

  initConfig();
  if (disabled_) {
    // The filter has been disabled for this route.
    return Http::FilterHeadersStatus::Continue;
  }

  callbacks_->setDecoderBufferLimit(config_->max_request_bytes_);
  request_timeout_ = callbacks_->dispatcher().createTimer([this]() -> void { onRequestTimeout(); });
  request_timeout_->enableTimer(config_->max_request_time_);

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus BufferFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    resetInternalState();
    return Http::FilterDataStatus::Continue;
  }

  // Buffer until the complete request has been processed or the ConnectionManagerImpl sends a 413.
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus BufferFilter::decodeTrailers(Http::HeaderMap&) {
  resetInternalState();
  return Http::FilterTrailersStatus::Continue;
}

BufferFilterStats BufferFilter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  std::string final_prefix = prefix + "buffer.";
  return {ALL_BUFFER_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

void BufferFilter::onDestroy() {
  resetInternalState();
  stream_destroyed_ = true;
}

void BufferFilter::onRequestTimeout() {
  Http::Utility::sendLocalReply(*callbacks_, stream_destroyed_, Http::Code::RequestTimeout,
                                "buffer request timeout");
  config_->stats_.rq_timeout_.inc();
}

void BufferFilter::resetInternalState() { request_timeout_.reset(); }

void BufferFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
