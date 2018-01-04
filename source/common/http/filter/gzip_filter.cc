#include "common/http/filter/gzip_filter.h"

#include <regex>

#include "common/common/macros.h"

namespace Envoy {
namespace Http {

namespace {
// Default zlib memory level.
const uint64_t DEFAULT_MEMORY_LEVEL{5};

// Default and maximum compression window size.
const uint64_t DEFAULT_WINDOW_BITS{12};

// Minimum length of an upstream response that allows compression.
const uint64_t MINIMUM_CONTENT_LENGTH{30};

// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GZIP_HEADER_VALUE{16};

// Default content types will be used if any is provided by the user.
const std::unordered_set<std::string> DEFAULT_CONTENT_TYPES{
    "text/html",        "text/plain",    "text/css", "application/javascript",
    "application/json", "image/svg+xml", "text/xml", "application/xhtml+xml"};

// Regex used to validate a gzip Accept-Encoding header.
const std::regex& acceptEncodingRegex() {
  CONSTRUCT_ON_FIRST_USE(std::regex, "(?!.*gzip;\\s*q=0(,|$))(?=(.*gzip)|(^\\*$))",
                         std::regex::optimize);
}
} // namespace

GzipFilterConfig::GzipFilterConfig(const envoy::api::v2::filter::http::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      content_length_(contentLengthUint(gzip.content_length().value())),
      memory_level_(memoryLevelUint(gzip.memory_level().value())),
      window_bits_(windowBitsUint(gzip.window_bits().value())),
      content_type_values_(contentTypeSet(gzip.content_type())),
      disable_on_etag_(gzip.disable_on_etag().value()),
      disable_on_last_modified_(gzip.disable_on_last_modified().value()),
      disable_vary_(gzip.disable_vary().value()) {}

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

std::unordered_set<std::string>
GzipFilterConfig::contentTypeSet(const Protobuf::RepeatedPtrField<std::string>& types) {
  return types.empty() ? DEFAULT_CONTENT_TYPES
                       : std::unordered_set<std::string>(types.cbegin(), types.cend());
}

uint64_t GzipFilterConfig::contentLengthUint(Protobuf::uint32 length) {
  return length >= MINIMUM_CONTENT_LENGTH ? length : MINIMUM_CONTENT_LENGTH;
}

uint64_t GzipFilterConfig::memoryLevelUint(Protobuf::uint32 level) {
  return level > 0 ? level : DEFAULT_MEMORY_LEVEL;
}

uint64_t GzipFilterConfig::windowBitsUint(Protobuf::uint32 window_bits) {
  return (window_bits > 0 ? window_bits : DEFAULT_WINDOW_BITS) | GZIP_HEADER_VALUE;
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
    // TODO(gsagula): tests needed.
    config_->disableVary()
        ? headers.removeVary()
        : headers.insertVary().value(Http::Headers::get().VaryValues.AcceptEncoding);

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
  const Http::HeaderEntry* content_length = headers.ContentLength();
  if (content_length) {
    uint64_t length{};
    StringUtil::atoul(content_length->value().c_str(), length);
    return length >= config_->minimumLength();
  }

  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  return (transfer_encoding && transfer_encoding->value().find(
                                   Http::Headers::get().TransferEncodingValues.Chunked.c_str()));
}

bool GzipFilter::isContentTypeAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type && !config_->contentTypeValues().empty()) {
    std::string content_type_str{content_type->value().c_str()};
    StringUtil::eraseFrom(content_type_str, ";");
    StringUtil::trim(content_type_str);
    return config_->contentTypeValues().find(content_type_str) !=
           config_->contentTypeValues().end();
  }

  return true;
}

bool GzipFilter::isCacheControlAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  return cache_control ? !cache_control->value().find(
                             Http::Headers::get().CacheControlValues.NoTransform.c_str())
                       : true;
}

bool GzipFilter::isTransferEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  return !(transfer_encoding && transfer_encoding->value().find(
                                    Http::Headers::get().TransferEncodingValues.Gzip.c_str()));
}

bool GzipFilter::isLastModifiedAllowed(HeaderMap& headers) const {
  return !(config_->disableOnLastModified() && headers.LastModified());
}

bool GzipFilter::isEtagAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* etag = headers.Etag();
  if (config_->disableOnEtag()) {
    return !etag;
  }

  // TODO(gsagula): tests needed / do it efficiently.
  if (etag && !etag->value().find("W/")) {
    std::string etag_str{"W/"};
    etag_str.append(etag->value().c_str());
    headers.removeEtag();
    headers.insertEtag().value(etag_str);
  }

  return true;
}

} // namespace Http
} // namespace Envoy
