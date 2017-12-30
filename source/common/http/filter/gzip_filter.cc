#include "common/http/filter/gzip_filter.h"

#include <regex>

#include "common/common/macros.h"

namespace Envoy {
namespace Http {

namespace {
const std::regex& acceptEncodingRegex() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "(?!.*gzip;\\s*q=0(,|$))(?=(.*gzip)|(^\\*$))",
                         std::regex::optimize);
}
} // namespace

// Default and maximum compression window size.
const uint64_t GzipFilterConfig::DEFAULT_WINDOW_BITS{15};
// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GzipFilterConfig::GZIP_HEADER_VALUE{16};
// Default zlib memory level.
const uint64_t GzipFilterConfig::DEFAULT_MEMORY_LEVEL{8};
// Minimum length of an upstream response that allows compression.
const uint64_t GzipFilterConfig::MINIMUM_CONTENT_LENGTH{30};

GzipFilterConfig::GzipFilterConfig(const envoy::api::v2::filter::http::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      content_length_(static_cast<uint64_t>(gzip.content_length().value())),
      memory_level_(static_cast<uint64_t>(gzip.memory_level().value())),
      window_bits_(static_cast<uint64_t>(gzip.window_bits().value())),
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

uint64_t GzipFilterConfig::memoryLevel() const {
  return memory_level_ > 0 ? memory_level_ : DEFAULT_MEMORY_LEVEL;
}

uint64_t GzipFilterConfig::minimumLength() const {
  return content_length_ > 29 ? content_length_ : MINIMUM_CONTENT_LENGTH;
}

uint64_t GzipFilterConfig::windowBits() const {
  return (window_bits_ > 0 ? window_bits_ : DEFAULT_WINDOW_BITS) | GZIP_HEADER_VALUE;
}

GzipFilter::GzipFilter(const GzipFilterConfigSharedPtr& config)
    : skip_compression_{true}, compressed_data_(), compressor_(), config_(config) {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  // TODO(gsagula): The current implementation checks for the presence of 'gzip' and if the same
  // is followed by Qvalue. Since gzip is the only available content-encoding in Envoy at the
  // moment, order/priority of preferred server encodings is disregarded (RFC2616-14.3).
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
    compressor_.init(config_->compressionLevel(), config_->compressionStrategy(),
                     config_->windowBits(), config_->memoryLevel());
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

  if (n_data) {
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

bool GzipFilter::isMinimumContentLength(HeaderMap& headers) const {
  if (headers.ContentLength()) {
    uint64_t content_length{};
    StringUtil::atoul(headers.ContentLength()->value().c_str(), content_length);
    return content_length >= config_->minimumLength();
  }

  return (headers.TransferEncoding() &&
          headers.TransferEncoding()->value().find(
              Http::Headers::get().TransferEncodingValues.Chunked.c_str()));
}

bool GzipFilter::isContentTypeAllowed(HeaderMap& headers) const {
  if (headers.ContentType() && !config_->contentTypeValues().empty()) {
    std::string content_type{headers.ContentType()->value().c_str()};
    StringUtil::eraseFrom(content_type, ";");
    StringUtil::trim(content_type);
    return config_->contentTypeValues().find(content_type) != config_->contentTypeValues().end();
  }

  return true;
}

bool GzipFilter::isCacheControlAllowed(HeaderMap& headers) const {
  if (headers.CacheControl() && !config_->cacheControlValues().empty()) {
    auto header_values = StringUtil::split(headers.CacheControl()->value().c_str(), ',');
    auto predicate = [&](const auto& header_value) {
      return config_->cacheControlValues().find(header_value) ==
             config_->cacheControlValues().end();
    };
    return !std::any_of(header_values.begin(), header_values.end(), predicate);
  }

  return true;
}

bool GzipFilter::isEtagAllowed(HeaderMap& headers) const {
  return !(headers.Etag() && config_->disableOnEtag());
}

bool GzipFilter::isLastModifiedAllowed(HeaderMap& headers) const {
  return !(headers.LastModified() && config_->disableOnLastModified());
}

bool GzipFilter::isTransferEncodingAllowed(HeaderMap& headers) const {
  return !(headers.TransferEncoding() &&
           headers.TransferEncoding()->value().find(
               Http::Headers::get().TransferEncodingValues.Gzip.c_str()));
}

} // namespace Http
} // namespace Envoy
