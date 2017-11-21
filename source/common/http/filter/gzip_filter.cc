#include "common/http/filter/gzip_filter.h"

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter(GzipFilterConfigSharedPtr config) : config_(config) {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (headers.AcceptEncoding() != nullptr) {
    request_headers_ = &headers;
  }
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool) {
  // TODO(gsagula): In order to fully implement RFC2616-14.3, more work is required here. The
  // current implementation only checks if `gzip` is found in `accept-encoding` header, but
  // it disregards the presence of qvalue or the order/priority of other encoding types.
  if (request_headers_ == nullptr || !(request_headers_->AcceptEncoding()->value().find(
                                           Headers::get().AcceptEncodingValues.Gzip.c_str()) ||
                                       request_headers_->AcceptEncoding()->value().find(
                                           Headers::get().AcceptEncodingValues.Wildcard.c_str()))) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Verifies that upstream has not been encoded already.
  if (headers.ContentEncoding() != nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Verifies that content-type is whitelisted and initializes compressor.
  if (isContentTypeAllowed(headers)) {
    headers.removeContentLength();
    headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
    response_headers_ = &headers;
    compressor_.init(config_->getCompressionLevel(),
                     Compressor::ZlibCompressorImpl::CompressionStrategy::Standard, 31,
                     config_->getMemoryLevel());
  }

  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_headers_ == nullptr || response_headers_->ContentEncoding() == nullptr ||
      !response_headers_->ContentEncoding()->value().find(
          Headers::get().ContentEncodingValues.Gzip.c_str())) {
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

bool GzipFilter::isContentTypeAllowed(const HeaderMap& headers) {
  if (config_->getContentTypes().size() == 0 || headers.ContentType() == nullptr) {
    return true;
  }
  for (const auto& content_type : config_->getContentTypes()) {
    if (headers.ContentType()->value().find(content_type.c_str())) {
      return true;
    }
  };

  return false;
}

} // namespace Http
} // namespace Envoy
