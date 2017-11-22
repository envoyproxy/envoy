#include "common/http/filter/gzip_filter.h"

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter(GzipFilterConfigSharedPtr config)
    : skipCompression_{true}, compressed_data_(), compressor_(), config_(config) {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  accept_encoding_ = headers.get(Http::Headers::get().AcceptEncoding);
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool) {
  // TODO(gsagula): In order to fully implement RFC2616-14.3, more work is required here. The
  // current implementation only checks if `gzip` is found in `accept-encoding` header, but
  // it disregards the presence of qvalue or the order/priority of other encoding types.
  if (accept_encoding_ == nullptr ||
      !(accept_encoding_->value().find(Headers::get().AcceptEncodingValues.Gzip.c_str()) ||
        accept_encoding_->value().find(Headers::get().AcceptEncodingValues.Wildcard.c_str()))) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Skip compression if upstream data is already encoded.
  if (headers.ContentEncoding() != nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Skip compression if content-type is not whitelisted.
  if (!isContentTypeAllowed(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Removing content-length will set transfer-encoding to chunked.
  headers.removeContentLength();
  headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
  compressor_.init(config_->getCompressionLevel(), config_->getCompressionStrategy(),
                   config_->getWindowBits(), config_->getMemoryLevel());

  skipCompression_ = false;
  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (skipCompression_) {
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
