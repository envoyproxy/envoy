#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Http {

// A decoder filter which passes all data through with Continue status.
class PassThroughDecoderFilter : public virtual StreamDecoderFilter {
public:
  // Explicit move assignment to eliminate virtual-move-assign warning.
  PassThroughDecoderFilter& operator=(PassThroughDecoderFilter&& filter) noexcept {
    PassThroughDecoderFilter::StreamDecoderFilter::operator=(std::move(filter));
    decoder_callbacks_ = filter.decoder_callbacks_;
    return *this;
  }
  PassThroughDecoderFilter& operator=(const PassThroughDecoderFilter&) = default;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
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
  // Explicit move assignment to eliminate virtual-move-assign warning.
  PassThroughEncoderFilter& operator=(PassThroughEncoderFilter&& filter) noexcept {
    PassThroughEncoderFilter::StreamEncoderFilter::operator=(std::move(filter));
    encoder_callbacks_ = filter.encoder_callbacks_;
    return *this;
  }
  PassThroughEncoderFilter& operator=(const PassThroughEncoderFilter&) = default;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
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
