#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A test filter that does StopIterationNoBuffer on end stream, then continues after a 0ms alarm.
class StopIterationAndContinueFilter : public Http::PassThroughFilter {
public:
  void setEndStreamAndDecodeTimer() {
    decode_end_stream_seen_ = true;
    decode_delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { decoder_callbacks_->continueDecoding(); });
    decode_delay_timer_->enableTimer(std::chrono::seconds(0));
  }

  void setEndStreamAndEncodeTimer() {
    encode_end_stream_seen_ = true;
    encode_delay_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() -> void { encoder_callbacks_->continueEncoding(); });
    encode_delay_timer_->enableTimer(std::chrono::seconds(0));
  }

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    if (end_stream) {
      setEndStreamAndDecodeTimer();
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    RELEASE_ASSERT(!decode_end_stream_seen_, "end stream seen twice");
    if (end_stream) {
      setEndStreamAndDecodeTimer();
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    if (end_stream) {
      setEndStreamAndEncodeTimer();
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    RELEASE_ASSERT(!encode_end_stream_seen_, "end stream seen twice");
    if (end_stream) {
      setEndStreamAndEncodeTimer();
    }
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  Event::TimerPtr decode_delay_timer_;
  bool decode_end_stream_seen_{};
  Event::TimerPtr encode_delay_timer_;
  bool encode_end_stream_seen_{};
};

class StopIterationAndContinueFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  StopIterationAndContinueFilterConfig()
      : EmptyHttpFilterConfig("stop-iteration-and-continue-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::StopIterationAndContinueFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<StopIterationAndContinueFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
