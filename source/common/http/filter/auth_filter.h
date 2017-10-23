#pragma once

#include "envoy/http/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Http {

/**
 * Configuration for the auth filter is in router filter
 */

/**
 * A filter that do route level auth.
 */
class AuthFilter : public StreamDecoderFilter, protected Logger::Loggable<Logger::Id::filter> {
public:
  AuthFilter();
  ~AuthFilter();

  // Http::StreamFilterBase
  void onDestroy() override { stream_reset_ = true; };

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;

  FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return FilterDataStatus::Continue;
  }

  FilterTrailersStatus decodeTrailers(HeaderMap&) override {
    return FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  };

private:
  StreamDecoderFilterCallbacks* callbacks_{};
  bool stream_reset_{};
};
} // namespace Http
} // namespace Envoy
