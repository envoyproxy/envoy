#include "common/http/filter/gzip_filter.h"
#include "common/common/assert.h"

#include <iostream>

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter() {
  this->ZlibPtr_ = std::unique_ptr<Zlib::Impl>(new Zlib::Impl());
}

GzipFilter::~GzipFilter() {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  Http::HeaderEntry* content_encoding_header = headers.AcceptEncoding();
  if (content_encoding_header &&
      content_encoding_header->value().find(
          Http::Headers::get().AcceptEncodingValues.Deflate.c_str())) {
    deflate_ = true;
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

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  content_encoding_header_ = headers.ContentEncoding();

  if (!deflate_ || end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Br.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Compress.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Deflate.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Gzip.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Gzip.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }
  
  is_compressed_ = false;

  headers.removeContentLength();
  headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Deflate);
  headers.removeEtag();

  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    std::cout << "total in: " << ZlibPtr_->getTotalIn() << std::endl;
    std::cout << "total out: " << ZlibPtr_->getTotalOut() << std::endl;
    return Http::FilterDataStatus::Continue;
  }
  
  if (!deflate_ || is_compressed_) {
    return Http::FilterDataStatus::Continue;
  }

  if (ZlibPtr_->deflateData(data)) {
    return Http::FilterDataStatus::Continue;
  } else { 
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
}

FilterTrailersStatus GzipFilter::encodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void GzipFilter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

} // Http
} // Envoy
