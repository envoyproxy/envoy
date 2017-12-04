#include "common/http/filter/gzip_filter.h"

#include <iostream>
#include <regex>

#include "common/common/logger.h"

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter(GzipFilterConfigSharedPtr config)
    : skip_compression_{true}, compressed_data_(), compressor_(), config_(config) {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  accept_encoding_ = headers.get(Http::Headers::get().AcceptEncoding);
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO(gsagula): In order to fully implement RFC2616-14.3, more work is required here. The
  // current implementation only checks if `gzip` is found in `accept-encoding` header, but
  // it disregards the presence of Qvalue (e.g. gzip;q=0) or the order/priority of other encoding
  // types.
  if (accept_encoding_ == nullptr ||
      !(accept_encoding_->value().find(Headers::get().AcceptEncodingValues.Gzip.c_str()) ||
        accept_encoding_->value().find(Headers::get().AcceptEncodingValues.Wildcard.c_str()))) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Skip compression when:
  //  upstream data is already encoded
  //  content-type is not supported.
  //  content-length is bellow the minimum treshold.
  //  cache-control is not allowed
  //  etag if disable_on_etag config
  //  last-modified if disable_on_last_modified config
  if (headers.ContentEncoding() != nullptr || !isContentTypeAllowed(headers) ||
      !isMinimumContentLength(headers) || !isCacheControlAllowed(headers) ||
      (config_->disableOnEtag() && headers.Etag() != nullptr) ||
      (config_->disableOnLastModified() && headers.LastModified() != nullptr)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Removing content-length will set transfer-encoding to chunked.
  headers.removeContentLength();
  headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
  compressor_.init(config_->getCompressionLevel(),
                   Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 31,
                   config_->getMemoryLevel());

  skip_compression_ = false;
  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (skip_compression_) {
    return Http::FilterDataStatus::Continue;
  }

  compressor_.compress(data, compressed_data_);

  if (end_stream) {
    compressor_.flush(compressed_data_);
  }

  if (compressed_data_.length() > 0) {
    const uint64_t n_data{data.length()};
    data.drain(n_data);
    data.move(compressed_data_);
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

bool GzipFilter::isContentTypeAllowed(const HeaderMap& headers) const {
  if (!config_->getContentTypeValues().size()) {
    return true;
  }

  if (headers.ContentType() == nullptr) {
    return false;
  }

  for (auto const& value : config_->getContentTypeValues()) {
    if (headers.ContentType()->value().find(value.c_str())) {
      return true;
    }
  }

  return false;
}

bool GzipFilter::isCacheControlAllowed(const HeaderMap& headers) const {
  if (headers.CacheControl() == nullptr || !config_->getCacheControlValues().size()) {
    return true;
  }

  for (auto const& value : config_->getCacheControlValues()) {
    if (headers.ContentType()->value().find(value.c_str())) {
      return true;
    }
  }

  return false;
}

bool GzipFilter::isMinimumContentLength(const HeaderMap& headers) const {
  if (headers.ContentLength() == nullptr) {
    return false;
  }

  uint64_t content_length;
  if (!StringUtil::atoul(headers.ContentLength()->value().c_str(), content_length)) {
    return false;
  }

  return (content_length >= config_->getMinimumLength());
}

} // namespace Http
} // namespace Envoy
