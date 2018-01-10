#include "common/http/filter/gzip_filter.h"

#include <forward_list>
#include <regex>

#include "common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

namespace {
// Default zlib memory level.
const uint64_t DefaultMemoryLevel{5};

// Default and maximum compression window size.
const uint64_t DefaultWindowBits{12};

// Minimum length of an upstream response that allows compression.
const uint64_t MinimumContentLength{30};

// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GzipHeaderValue{16};

// Used for verifying accept-encoding values.
const std::string ZeroQvalueString{"q=0"};

// Default content types will be used if any is provided by the user.
const std::unordered_set<std::string> defaultContentEncoding() {
  CONSTRUCT_ON_FIRST_USE(std::unordered_set<std::string>,
                         {"text/html", "text/plain", "text/css", "application/javascript",
                          "application/json", "image/svg+xml", "text/xml",
                          "application/xhtml+xml"});
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
      disable_on_last_modified_(gzip.disable_on_last_modified().value()) {}

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
  return types.empty() ? defaultContentEncoding()
                       : std::unordered_set<std::string>(types.cbegin(), types.cend());
}

uint64_t GzipFilterConfig::contentLengthUint(Protobuf::uint32 length) {
  return length >= MinimumContentLength ? length : MinimumContentLength;
}

uint64_t GzipFilterConfig::memoryLevelUint(Protobuf::uint32 level) {
  return level > 0 ? level : DefaultMemoryLevel;
}

uint64_t GzipFilterConfig::windowBitsUint(Protobuf::uint32 window_bits) {
  return (window_bits > 0 ? window_bits : DefaultWindowBits) | GzipHeaderValue;
}

GzipFilter::GzipFilter(const GzipFilterConfigSharedPtr& config)
    : skip_compression_{true}, compressed_data_(), compressor_(), config_(config) {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  skip_compression_ = !isAcceptEncodingAllowed(headers);
  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (!end_stream && !skip_compression_ && isMinimumContentLength(headers) &&
      isContentTypeAllowed(headers) && isCacheControlAllowed(headers) && isEtagAllowed(headers) &&
      isLastModifiedAllowed(headers) && isTransferEncodingAllowed(headers) &&
      !headers.ContentEncoding()) {

    insertVary(headers);
    headers.removeContentLength();
    headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Gzip);
    compressor_.init(config_->compressionLevel(), config_->compressionStrategy(),
                     config_->windowBits(), config_->memoryLevel());

  } else {
    skip_compression_ = true;
  }

  return Http::FilterHeadersStatus::Continue;
}

// TODO(gsagula): We need to test whether or not the compressed output is larger than
// the uncompressed input. This could potentially be done by forcing flush on the first
// few buffers received and keeping a copy of the uncompressed ones. If compression is
// below certain expected ratio, the compressed buffer is disposed and the original
// response/headers are passed to the client instead.
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

bool GzipFilter::isAcceptEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* accept_encoding = headers.AcceptEncoding();
  // TODO(gsagula): Since gzip is the only available content-encoding in Envoy at the moment,
  // order/priority of preferred server encodings is disregarded (RFC2616-14.3). Replace this
  // with a data structure that parses Accept-Encoding values and allows fast lookup of
  // key/priority. Also, this should be part of some utility library.
  if (accept_encoding) {
    bool is_identity{true};  // true if does not exist or is followed by q=0.
    bool is_wildcard{false}; // true if found and not followed by `q=0`.
    bool is_gzip{false};     // true if exists and not followed by `q=0`.

    auto tokens = StringViewUtil::splitToken(headers.AcceptEncoding()->value().c_str(), ";,");
    for (auto it = tokens.begin(); it != tokens.end(); it++) {
      if (*it == Http::Headers::get().AcceptEncodingValues.Gzip) {
        is_gzip = *(it + 1) != ZeroQvalueString;
      }
      if (*it == Http::Headers::get().AcceptEncodingValues.Identity) {
        is_identity = *(it + 1) == ZeroQvalueString;
      }
      if (*it == Http::Headers::get().AcceptEncodingValues.Wildcard) {
        is_wildcard = *(it + 1) != ZeroQvalueString;
      }
    }

    return ((is_gzip && is_identity) || is_wildcard);
  }

  return true;
}

bool GzipFilter::isCacheControlAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  if (cache_control) {
    return !StringViewUtil::findToken(cache_control->value().c_str(), ";=",
                                      Http::Headers::get().CacheControlValues.NoTransform.c_str());
  }
  return true;
}

bool GzipFilter::isContentTypeAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type && !config_->contentTypeValues().empty()) {
    // TODO(gsagula): do this lookup without allocating a new string.
    std::string value{StringViewUtil::cropRight(content_type->value().c_str(), ";")};
    return config_->contentTypeValues().find(value) != config_->contentTypeValues().end();
  }
  return true;
}

bool GzipFilter::isEtagAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* etag = headers.Etag();
  if (config_->disableOnEtag()) {
    return !etag;
  }
  if (etag) {
    absl::string_view value(etag->value().c_str());
    return (value.length() > 2 && (value[0] == 'w' || value[0] == 'W') && value[1] == '/');
  }
  return true;
}

bool GzipFilter::isLastModifiedAllowed(HeaderMap& headers) const {
  return !(config_->disableOnLastModified() && headers.LastModified());
}

bool GzipFilter::isMinimumContentLength(HeaderMap& headers) const {
  const Http::HeaderEntry* content_length = headers.ContentLength();
  if (content_length) {
    uint64_t length;
    return StringUtil::atoul(content_length->value().c_str(), length)
               ? length >= config_->minimumLength()
               : false;
  }

  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  return (transfer_encoding &&
          StringViewUtil::findToken(transfer_encoding->value().c_str(), ",",
                                    Http::Headers::get().TransferEncodingValues.Chunked.c_str()));
}

bool GzipFilter::isTransferEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  if (transfer_encoding) {
    for (auto header_value : StringViewUtil::splitToken(transfer_encoding->value().c_str(), ",")) {
      if (header_value == Http::Headers::get().TransferEncodingValues.Gzip ||
          header_value == Http::Headers::get().TransferEncodingValues.Deflate) {
        return false;
      }
    }
  }
  return true;
}

void GzipFilter::insertVary(HeaderMap& headers) {
  const Http::HeaderEntry* vary = headers.Vary();
  if (vary) {
    auto header_values = StringViewUtil::splitToken(vary->value().c_str(), ",");
    bool has_accept_encoding{false};

    std::ostringstream oss;
    for (auto it = header_values.begin(); it != header_values.end(); it++) {
      if (*it != Http::Headers::get().VaryValues.Wildcard) {
        oss << *it;
      }
      if (*it != header_values.back() && it != header_values.begin()) {
        oss << ", ";
      }
      if (*it == Http::Headers::get().VaryValues.AcceptEncoding) {
        has_accept_encoding = true;
      }
    }

    if (!has_accept_encoding) {
      oss << ", " << Http::Headers::get().VaryValues.AcceptEncoding;
    }

    headers.insertVary().value(oss.str());
  } else {
    headers.insertVary().value(Http::Headers::get().VaryValues.AcceptEncoding);
  }
}

} // namespace Http
} // namespace Envoy
