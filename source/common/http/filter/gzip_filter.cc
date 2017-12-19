#include "common/http/filter/gzip_filter.h"

#include <regex>

#include "common/common/macros.h"

namespace Envoy {
namespace Http {

static const std::regex& acceptEncodingRegex() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "(?!.*gzip;\\s*q=0(,|$))(?=(.*gzip)|(^\\*$))",
                         std::regex::optimize);
}

GzipFilterConfig::GzipFilterConfig(const envoy::api::v2::filter::http::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      content_length_(static_cast<uint64_t>(gzip.content_length().value())),
      memory_level_(static_cast<uint64_t>(gzip.memory_level().value())),
      cache_control_values_(gzip.cache_control().cbegin(), gzip.cache_control().cend()),
      content_type_values_(gzip.content_type().cbegin(), gzip.content_type().cend()),
      etag_(gzip.disable_on_etag().value()),
      last_modified_(gzip.disable_on_last_modified().value()) {}

ZlibCompressionLevelEnum
GzipFilterConfig::compressionLevelEnum(const GzipV2CompressionLevelEnum& compression_level) {
  switch (compression_level) {
  case GzipV2CompressionLevelEnum::Gzip_CompressionLevel_Enum_BEST:
    return ZlibCompressionLevelEnum::Best;
  case GzipV2CompressionLevelEnum::Gzip_CompressionLevel_Enum_SPEED:
    return ZlibCompressionLevelEnum::Speed;
  default:
    return ZlibCompressionLevelEnum::Standard;
  }
}

ZlibCompressionStrategyEnum GzipFilterConfig::compressionStrategyEnum(
    const GzipV2CompressionStrategyEnum& compression_strategy) {
  switch (compression_strategy) {
  case GzipV2CompressionStrategyEnum::Gzip_CompressionStrategy_RLE:
    return ZlibCompressionStrategyEnum::Rle;
  case GzipV2CompressionStrategyEnum::Gzip_CompressionStrategy_FILTERED:
    return ZlibCompressionStrategyEnum::Filtered;
  case GzipV2CompressionStrategyEnum::Gzip_CompressionStrategy_HUFFMAN:
    return ZlibCompressionStrategyEnum::Huffman;
  default:
    return ZlibCompressionStrategyEnum::Standard;
  }
}

// By adding 16 to the default and max window_bits(15), a gzip header and a trailer will be placed
// around the compressed data.
const uint64_t GzipFilter::WINDOW_BITS{15 | 16};

GzipFilter::GzipFilter(GzipFilterConfigSharedPtr config)
    : skip_compression_{true}, compressed_data_(), compressor_(), config_(config) {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  // TODO(gsagula): The current implementation checks for the presence of 'gzip' and if the same
  // is followed by Qvalue. Since gzip is the only available encoding right now, order/priority of
  // preferred server encodings is disregarded (RFC2616-14.3).
  skip_compression_ =
      !(headers.AcceptEncoding() &&
        std::regex_search(headers.AcceptEncoding()->value().c_str(), acceptEncodingRegex()));
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (!end_stream && !skip_compression_ && isMinimumContentLength(headers) &&
      isContentTypeAllowed(headers) && isCacheControlAllowed(headers) && isEtagAllowed(headers) &&
      isLastModifiedAllowed(headers) && isTransferEncodingAllowed(headers) &&
      !headers.ContentEncoding()) {
    headers.removeContentLength();
    headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
    compressor_.init(config_->compressionLevel(), config_->compressionStrategy(), WINDOW_BITS,
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

  if (compressed_data_.length()) {
    data.drain(n_data);
    data.move(compressed_data_);
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

bool GzipFilter::isContentTypeAllowed(const HeaderMap& headers) const {
  if (config_->contentTypeValues().size() && headers.ContentType()) {
    return std::any_of(config_->contentTypeValues().begin(), config_->contentTypeValues().end(),
                       [&headers](const auto& value) {
                         return headers.ContentType()->value().find(value.c_str());
                       });
  }
  return true;
}

bool GzipFilter::isCacheControlAllowed(const HeaderMap& headers) const {
  if (config_->cacheControlValues().size() && headers.CacheControl()) {
    return std::any_of(config_->cacheControlValues().begin(), config_->cacheControlValues().end(),
                       [&headers](const auto& value) {
                         return headers.CacheControl()->value().find(value.c_str());
                       });
  }
  return true;
}

bool GzipFilter::isMinimumContentLength(const HeaderMap& headers) const {
  uint64_t content_length;
  if (headers.ContentLength() &&
      StringUtil::atoul(headers.ContentLength()->value().c_str(), content_length)) {
    return content_length >= config_->minimumLength();
  }
  return false;
}

bool GzipFilter::isEtagAllowed(const HeaderMap& headers) const {
  return config_->disableOnEtag() ? !headers.Etag() : true;
}

bool GzipFilter::isLastModifiedAllowed(const HeaderMap& headers) const {
  return config_->disableOnLastModified() ? !headers.LastModified() : true;
}

bool GzipFilter::isTransferEncodingAllowed(const HeaderMap& headers) const {
  return headers.TransferEncoding() != nullptr
             ? !headers.TransferEncoding()->value().find(
                   Http::Headers::get().TransferEncodingValues.Gzip.c_str())
             : true;
}

} // namespace Http
} // namespace Envoy
