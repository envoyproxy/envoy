#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/http/header_map_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/compressor/zlib_compressor_impl.h"

namespace Envoy {
namespace Http {

class GzipFilter : public Http::StreamFilter {
public:
  GzipFilter();
  ~GzipFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool) override;
  FilterDataStatus decodeData(Buffer::Instance& buffer, bool) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& headers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  FilterHeadersStatus encodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  FilterTrailersStatus encodeTrailers(HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override;

private:
  StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  Http::HeaderEntry* content_encoding_header_{nullptr};
  Compressor::ZlibCompressorImpl compressor_;
  Buffer::OwnedImpl buffer_;
  
  bool deflate_{false};
  bool is_compressed_{true};
  
};

} // Http
} // Envoy
