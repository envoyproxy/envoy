#include "common/http/filter/gzip_filter.h"

#include <chrono>
#include <iostream>
#include <thread>

#include "common/common/assert.h"

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter() {}

GzipFilter::~GzipFilter() {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (headers.AcceptEncoding() &&
      headers.AcceptEncoding()->value().find(Headers::get().AcceptEncodingValues.Deflate.c_str())) {
    compress_ = true;
  }

  return FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus GzipFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void GzipFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool) {
  if (compress_ && headers.ContentEncoding() == nullptr) {
    compressor_.init(Compressor::ZlibCompressorImpl::CompressionLevel::default_compression,
                     Compressor::ZlibCompressorImpl::CompressionStrategy::default_strategy,
                     gzip_header_, memory_level_);
    // TODO: Just for testing.. remove it later
    decompressor_.init(gzip_header_);
    headers.removeContentLength();
    headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
    headers.removeEtag();
  } else {
    compress_ = false;
  }

  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool) {
  if (!compress_) {
    return Http::FilterDataStatus::Continue;
  }

  uint64_t length{data.length()};
  compressor_.compress(data, buffer_);
  data.drain(length);

  // TODO: Just for testing.. remove it later
  decompressor_.decompress(buffer_, data);
  buffer_.drain(buffer_.length());

  return Http::FilterDataStatus::Continue;
}

FilterTrailersStatus GzipFilter::encodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void GzipFilter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

} // namespace Http
} // namespace Envoy
