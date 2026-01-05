#include "source/extensions/filters/http/async_example/async_example.h"

#include "source/extensions/filters/http/async_example/v3/async_example.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AsyncExample {

AsyncExampleConfig::AsyncExampleConfig(
    const envoy::extensions::filters::http::async_example::v3::AsyncExample& proto_config)
    : delay_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, delay, 5)) {}

AsyncExampleFilter::AsyncExampleFilter(AsyncExampleConfigSharedPtr config,
                                       Event::Dispatcher& dispatcher)
    : config_(std::move(config)) {
  delay_timer_ = dispatcher.createTimer([this]() { onTimer(); });
}

AsyncExampleFilter::~AsyncExampleFilter() {}

std::string dataDebugString(const Envoy::Buffer::Instance& data) {
  const size_t kMaxStringLength = 20;
  std::string data_string = data.toString();
  if (data_string.size() <= kMaxStringLength) {
    return absl::StrFormat("size: %d content: [%s]", data_string.size(), data_string);
  }
  return absl::StrFormat("size: %d begin: [%s] end: [%s]", data_string.size(),
                         data_string.substr(0, kMaxStringLength),
                         data_string.substr(data_string.size() - kMaxStringLength));
}

void AsyncExampleFilter::onDestroy() {
  if (delay_timer_) {
    delay_timer_->disableTimer();
    delay_timer_.reset();
  }
}

Http::FilterHeadersStatus AsyncExampleFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus AsyncExampleFilter::decodeData(Buffer::Instance& data, bool) {
  ENVOY_LOG(error, "decodeData {}", dataDebugString(data));
  if (!async_complete_ && delay_timer_ && !delay_timer_->enabled()) {
    ENVOY_LOG(error, "Starting async delay for {} ms", config_->delay().count());
    delay_timer_->enableTimer(config_->delay());
    return Http::FilterDataStatus::StopIterationAndWatermark;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus AsyncExampleFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void AsyncExampleFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void AsyncExampleFilter::onTimer() {
  ENVOY_LOG(error, "Async delay finished, resuming decoding");
  async_complete_ = true;
  decoder_callbacks_->continueDecoding();
}

} // namespace AsyncExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
