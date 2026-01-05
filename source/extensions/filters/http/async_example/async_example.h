#pragma once

#include "source/extensions/filters/http/async_example/v3/async_example.pb.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AsyncExample {

class AsyncExampleConfig {
public:
  AsyncExampleConfig(const envoy::extensions::filters::http::async_example::v3::AsyncExample& proto_config);

  std::chrono::milliseconds delay() const { return delay_; }

private:
  const std::chrono::milliseconds delay_;
};

using AsyncExampleConfigSharedPtr = std::shared_ptr<AsyncExampleConfig>;

class AsyncExampleFilter : public Http::StreamDecoderFilter,
                           public Logger::Loggable<Logger::Id::filter> {
public:
  AsyncExampleFilter(AsyncExampleConfigSharedPtr config, Event::Dispatcher& dispatcher);
  ~AsyncExampleFilter() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  void onDestroy() override;

private:
  void onTimer();

  AsyncExampleConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Event::TimerPtr delay_timer_;
  bool async_complete_{false};
};

} // namespace AsyncExample
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
