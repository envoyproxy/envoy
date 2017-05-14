#include "common/http/filter/buffer_filter.h"

#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/codes.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Http {

BufferFilter::BufferFilter(BufferFilterConfigConstSharedPtr config) : config_(config) {}

BufferFilter::~BufferFilter() { ASSERT(!request_timeout_); }

FilterHeadersStatus BufferFilter::decodeHeaders(HeaderMap&, bool end_stream) {
  if (end_stream) {
    // If this is a header only request, we don't need to do any buffering.
    return FilterHeadersStatus::Continue;
  } else {
    // Otherwise stop further iteration.
    request_timeout_ =
        callbacks_->dispatcher().createTimer([this]() -> void { onRequestTimeout(); });
    request_timeout_->enableTimer(config_->max_request_time_);
    return FilterHeadersStatus::StopIteration;
  }
}

FilterDataStatus BufferFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    resetInternalState();
    return FilterDataStatus::Continue;
  } else if (callbacks_->decodingBuffer() &&
             callbacks_->decodingBuffer()->length() > config_->max_request_bytes_) {
    Http::HeaderMapPtr response_headers{new HeaderMapImpl{
        {Headers::get().Status, std::to_string(enumToInt(Http::Code::PayloadTooLarge))}}};
    callbacks_->encodeHeaders(std::move(response_headers), true);
    config_->stats_.rq_too_large_.inc();
  }

  return FilterDataStatus::StopIterationAndBuffer;
}

FilterTrailersStatus BufferFilter::decodeTrailers(HeaderMap&) {
  resetInternalState();
  return FilterTrailersStatus::Continue;
}

BufferFilterStats BufferFilter::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = prefix + "buffer.";
  return {ALL_BUFFER_FILTER_STATS(POOL_COUNTER_PREFIX(store, final_prefix))};
}

void BufferFilter::onDestroy() { resetInternalState(); }

void BufferFilter::onRequestTimeout() {
  Http::HeaderMapPtr response_headers{new HeaderMapImpl{
      {Headers::get().Status, std::to_string(enumToInt(Http::Code::RequestTimeout))}}};
  callbacks_->encodeHeaders(std::move(response_headers), true);
  config_->stats_.rq_timeout_.inc();
}

void BufferFilter::resetInternalState() { request_timeout_.reset(); }

void BufferFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // Http
