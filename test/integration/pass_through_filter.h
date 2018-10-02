#pragma once

#include "envoy/http/filter.h"

namespace Envoy {

// TODO(alyssawilk) move add_trailers_filter to use this.
// A filter which passes all data through with Continue status.
class PassThroughFilter : public Http::StreamFilter {
public:
  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

protected:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};
} // namespace Envoy
