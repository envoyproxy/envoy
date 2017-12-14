#include "common/http/filter/gzip_filter.h"

#include <regex>

#include "common/common/macros.h"

namespace Envoy {
namespace Http {

static const std::regex& acceptEncodingRegex() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "(?!.*gzip;\\s*q=0(,|$))(?=(.*gzip)|(^\\*$))",
                         std::regex::optimize);
}

GzipFilter::GzipFilter(GzipFilterConfigSharedPtr config)
    : skip_compression_{true}, compressed_data_(), compressor_(), config_(config) {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  // TODO(gsagula): The current implementation checks for the presence of 'gzip' and if the same
  // is followed by Qvalue. Since gzip is the only available encoding right now, order/priority of
  // preferred server encodings is disregarded (RFC2616-14.3).
  skip_compression_ = !isAcceptEncodingGzip(headers);
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (end_stream || skip_compression_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (isMinimumContentLength(headers) && isContentTypeAllowed(headers) &&
      isCacheControlAllowed(headers) && isEtagAllowed(headers) && isLastModifiedAllowed(headers) &&
      isTransferEncodingAllowed(headers) && headers.ContentEncoding() == nullptr) {
    headers.removeContentLength();
    headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
    compressor_.init(config_->compressionLevel(), config_->compressionStrategy(), 31,
                     config_->memoryLevel());
  } else {
    skip_compression_ = true;
  }

  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (skip_compression_) {
    return Http::FilterDataStatus::Continue;
  }

  const uint64_t n_data{data.length()};

  if (n_data > 0) {
    compressor_.compress(data, compressed_data_);
  }

  if (end_stream) {
    compressor_.flush(compressed_data_);
  }

  if (compressed_data_.length() > 0) {
    data.drain(n_data);
    data.move(compressed_data_);
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

bool GzipFilter::isAcceptEncodingGzip(const HeaderMap& headers) const {
  if (headers.AcceptEncoding() != nullptr) {
    return std::regex_search(headers.AcceptEncoding()->value().c_str(), acceptEncodingRegex());
  }
  return false;
}

bool GzipFilter::isContentTypeAllowed(const HeaderMap& headers) const {
  if (config_->contentTypeValues().size() > 0 && headers.ContentType() != nullptr) {
    for (const auto& value : config_->contentTypeValues()) {
      if (headers.ContentType()->value().find(value.c_str())) {
        return true;
      }
    }
    return false;
  }
  return true;
}

bool GzipFilter::isCacheControlAllowed(const HeaderMap& headers) const {
  if (config_->cacheControlValues().size() > 0 && headers.CacheControl() != nullptr) {
    for (const auto& value : config_->cacheControlValues()) {
      if (headers.CacheControl()->value().find(value.c_str())) {
        return true;
      }
    }
    return false;
  }
  return true;
}

bool GzipFilter::isMinimumContentLength(const HeaderMap& headers) const {
  uint64_t content_length;
  if (headers.ContentLength() != nullptr &&
      StringUtil::atoul(headers.ContentLength()->value().c_str(), content_length)) {
    return content_length >= config_->minimumLength();
  }
  return false;
}

bool GzipFilter::isEtagAllowed(const HeaderMap& headers) const {
  return config_->disableOnEtag() ? headers.Etag() == nullptr : true;
}

bool GzipFilter::isLastModifiedAllowed(const HeaderMap& headers) const {
  return config_->disableOnLastModified() ? headers.LastModified() == nullptr : true;
}

bool GzipFilter::isTransferEncodingAllowed(const HeaderMap& headers) const {
  return headers.TransferEncoding() != nullptr
             ? !headers.TransferEncoding()->value().find(
                   Http::Headers::get().TransferEncodingValues.Gzip.c_str())
             : true;
}

} // namespace Http
} // namespace Envoy
