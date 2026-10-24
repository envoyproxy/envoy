#include <chrono>
#include <string>

#include "envoy/event/timer.h"
#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"
#include "test/integration/filters/test_filters.pb.h"

#include "absl/strings/ascii.h"

namespace Envoy {

// Holds the last body frame or the trailers of each direction, and continues after a delay that
// is longer than the one of UppercaseBufferFilter.
class HoldEndStreamFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "hold-end-stream-filter";

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    if (!end_stream) {
      return Http::FilterDataStatus::Continue;
    }
    holdDecoding();
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    holdDecoding();
    return Http::FilterTrailersStatus::StopIteration;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    if (!end_stream) {
      return Http::FilterDataStatus::Continue;
    }
    holdEncoding();
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    holdEncoding();
    return Http::FilterTrailersStatus::StopIteration;
  }

private:
  void holdDecoding() {
    decode_timer_ = decoder_callbacks_->dispatcher().createTimer(
        [this]() { decoder_callbacks_->continueDecoding(); });
    decode_timer_->enableTimer(std::chrono::milliseconds(400));
  }

  void holdEncoding() {
    encode_timer_ = encoder_callbacks_->dispatcher().createTimer(
        [this]() { encoder_callbacks_->continueEncoding(); });
    encode_timer_->enableTimer(std::chrono::milliseconds(400));
  }

  Event::TimerPtr decode_timer_;
  Event::TimerPtr encode_timer_;
};

// Upper-cases every body byte it sees and marks the trailers it sees. Buffers everything after the
// headers until a delay after the first body frame or trailers, then streams the rest.
class UppercaseBufferFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "uppercase-buffer-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    uppercase(data);
    if (decode_continued_) {
      return Http::FilterDataStatus::Continue;
    }
    armDecodeTimer();
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override {
    trailers.addCopy(Http::LowerCaseString("uppercase-filter"), "seen");
    if (decode_continued_) {
      return Http::FilterTrailersStatus::Continue;
    }
    armDecodeTimer();
    return Http::FilterTrailersStatus::StopIteration;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    uppercase(data);
    if (encode_continued_) {
      return Http::FilterDataStatus::Continue;
    }
    armEncodeTimer();
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    trailers.addCopy(Http::LowerCaseString("uppercase-filter"), "seen");
    if (encode_continued_) {
      return Http::FilterTrailersStatus::Continue;
    }
    armEncodeTimer();
    return Http::FilterTrailersStatus::StopIteration;
  }

private:
  static void uppercase(Buffer::Instance& data) {
    std::string body = data.toString();
    absl::AsciiStrToUpper(&body);
    data.drain(data.length());
    data.add(body);
  }

  void armDecodeTimer() {
    if (decode_timer_ != nullptr) {
      return;
    }
    decode_timer_ = decoder_callbacks_->dispatcher().createTimer([this]() {
      decode_continued_ = true;
      decoder_callbacks_->continueDecoding();
    });
    decode_timer_->enableTimer(std::chrono::milliseconds(200));
  }

  void armEncodeTimer() {
    if (encode_timer_ != nullptr) {
      return;
    }
    encode_timer_ = encoder_callbacks_->dispatcher().createTimer([this]() {
      encode_continued_ = true;
      encoder_callbacks_->continueEncoding();
    });
    encode_timer_->enableTimer(std::chrono::milliseconds(200));
  }

  Event::TimerPtr decode_timer_;
  Event::TimerPtr encode_timer_;
  bool decode_continued_{};
  bool encode_continued_{};
};

static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<HoldEndStreamFilter,
                             test::integration::filters::HoldEndStreamFilterConfig>,
    Server::Configuration::NamedHttpFilterConfigFactory>
    hold_register_;
static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<HoldEndStreamFilter,
                             test::integration::filters::HoldEndStreamFilterConfig>,
    Server::Configuration::UpstreamHttpFilterConfigFactory>
    hold_register_upstream_;
static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<UppercaseBufferFilter,
                             test::integration::filters::UppercaseBufferFilterConfig>,
    Server::Configuration::NamedHttpFilterConfigFactory>
    uppercase_register_;
static Registry::RegisterFactory<
    UniqueSimpleFilterConfig<UppercaseBufferFilter,
                             test::integration::filters::UppercaseBufferFilterConfig>,
    Server::Configuration::UpstreamHttpFilterConfigFactory>
    uppercase_register_upstream_;

} // namespace Envoy
