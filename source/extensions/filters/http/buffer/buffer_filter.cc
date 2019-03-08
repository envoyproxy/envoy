#include "extensions/filters/http/buffer/buffer_filter.h"

#include "envoy/event/dispatcher.h"
#include "envoy/http/codes.h"

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

BufferFilterSettings::BufferFilterSettings(
    const envoy::config::filter::http::buffer::v2::Buffer& proto_config)
    : disabled_(false),
      max_request_bytes_(static_cast<uint64_t>(proto_config.max_request_bytes().value())) {}

BufferFilterSettings::BufferFilterSettings(
    const envoy::config::filter::http::buffer::v2::BufferPerRoute& proto_config)
    : disabled_(proto_config.disabled()),
      max_request_bytes_(
          proto_config.has_buffer()
              ? static_cast<uint64_t>(proto_config.buffer().max_request_bytes().value())
              : 0) {}

BufferFilterConfig::BufferFilterConfig(
    const envoy::config::filter::http::buffer::v2::Buffer& proto_config)
    : settings_(proto_config) {}

BufferFilter::BufferFilter(BufferFilterConfigSharedPtr config)
    : config_(config), settings_(config->settings()) {}

void BufferFilter::initConfig() {
  ASSERT(!config_initialized_);
  config_initialized_ = true;

  settings_ = config_->settings();

  if (!callbacks_->route() || !callbacks_->route()->routeEntry()) {
    return;
  }

  const std::string& name = HttpFilterNames::get().Buffer;
  const auto* entry = callbacks_->route()->routeEntry();

  const BufferFilterSettings* tmp = entry->perFilterConfigTyped<BufferFilterSettings>(name);
  const BufferFilterSettings* route_local =
      tmp ? tmp : entry->virtualHost().perFilterConfigTyped<BufferFilterSettings>(name);

  settings_ = route_local ? route_local : settings_;
}

Http::FilterHeadersStatus BufferFilter::decodeHeaders(Http::HeaderMap&, bool end_stream) {
  if (end_stream) {
    // If this is a header-only request, we don't need to do any buffering.
    return Http::FilterHeadersStatus::Continue;
  }

  initConfig();
  if (settings_->disabled()) {
    // The filter has been disabled for this route.
    return Http::FilterHeadersStatus::Continue;
  }

  callbacks_->setDecoderBufferLimit(settings_->maxRequestBytes());

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus BufferFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream || settings_->disabled()) {
    return Http::FilterDataStatus::Continue;
  }

  // Buffer until the complete request has been processed or the ConnectionManagerImpl sends a 413.
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

Http::FilterTrailersStatus BufferFilter::decodeTrailers(Http::HeaderMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void BufferFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
