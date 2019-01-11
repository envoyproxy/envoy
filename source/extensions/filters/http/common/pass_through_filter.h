#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Http {

// A decoder filter which passes all data through with Continue status.
class PassThroughDecoderFilter : public virtual StreamDecoderFilter {
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

protected:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
};

// An encoder filter which passes all data through with Continue status.
class PassThroughEncoderFilter : public virtual StreamEncoderFilter {
public:
  // Http::StreamFilterBase
  void onDestroy() override {}

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
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

// A filter which passes all data through with Continue status.
class PassThroughFilter : public StreamFilter,
                          public PassThroughDecoderFilter,
                          public PassThroughEncoderFilter {};
} // namespace Http
} // namespace Envoy
