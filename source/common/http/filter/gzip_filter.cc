#include "common/http/filter/gzip_filter.h"

#include <regex>

#include "common/common/macros.h"

#include "absl/strings/str_split.h"
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
const char ZeroQvalueString[] = "q=0";

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
      disable_on_etag_header_(gzip.disable_on_etag_header()),
      disable_on_last_modified_header_(gzip.disable_on_last_modified_header()) {}

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

    insertVaryHeader(headers);
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

bool GzipFilter::isAcceptEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* accept_encoding = headers.AcceptEncoding();
  // TODO(gsagula): Since gzip is the only available content-encoding in Envoy at the moment,
  // order/priority of preferred server encodings is disregarded (RFC2616-14.3). Replace this
  // with a data structure that parses Accept-Encoding values and allows fast lookup of
  // key/priority. Also, this should be part of some utility library.
  if (accept_encoding) {
    bool is_identity{true};  // true if does not exist or is followed by `q=0`.
    bool is_wildcard{false}; // true if found and not followed by `q=0`.
    bool is_gzip{false};     // true if exists and not followed by `q=0`.

    for (const auto token :
         StringUtil::splitToken(headers.AcceptEncoding()->value().c_str(), ",", true)) {
      const auto values = StringUtil::splitToken(token, ";");
      const auto value = StringUtil::trim(values.at(0));
      const auto q_value =
          values.size() > 1 ? StringUtil::trim(values.at(1)) : absl::string_view{nullptr, 0};

      if (value == Http::Headers::get().AcceptEncodingValues.Gzip) {
        is_gzip = q_value.empty() ? true : (q_value != ZeroQvalueString);
      }
      if (value == Http::Headers::get().AcceptEncodingValues.Identity) {
        is_identity = q_value.empty() ? false : (q_value == ZeroQvalueString);
      }
      if (value == Http::Headers::get().AcceptEncodingValues.Wildcard) {
        is_wildcard = q_value.empty() ? true : (q_value != ZeroQvalueString);
      }
    }

    return ((is_gzip && is_identity) || is_wildcard);
  }

  return true;
}

bool GzipFilter::isCacheControlAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  if (cache_control) {
    return !StringUtil::findToken(cache_control->value().c_str(), ",",
                                  Http::Headers::get().CacheControlValues.NoTransform.c_str());
  }
  return true;
}

bool GzipFilter::isContentTypeAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type && !config_->contentTypeValues().empty()) {
    std::string value{StringUtil::cropRight(content_type->value().c_str(), ";")};
    return config_->contentTypeValues().find(value) != config_->contentTypeValues().end();
  }
  return true;
}

// TODO(gsagula): It seems that every proxy has a different opinion how to handle Etag. Some
// discussions around this topic have been going on for over a decade, e.g.,
// https://bz.apache.org/bugzilla/show_bug.cgi?id=45023
// This design attempts to stay more on the safe side by preserving weak etags and removing
// the strong ones when disable_on_etag_header is false. Envoy does NOT rewrite Etags.
bool GzipFilter::isEtagAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* etag = headers.Etag();
  if (config_->disableOnEtagHeader()) {
    return !etag;
  }
  if (etag) {
    absl::string_view value(etag->value().c_str());
    if (value.length() < 2 || !((value[0] == 'w' || value[0] == 'W') && value[1] == '/')) {
      headers.removeEtag();
    }
  }
  return true;
}

bool GzipFilter::isLastModifiedAllowed(HeaderMap& headers) const {
  return !(config_->disableOnLastModifiedHeader() && headers.LastModified());
}

bool GzipFilter::isMinimumContentLength(HeaderMap& headers) const {
  const Http::HeaderEntry* content_length = headers.ContentLength();
  if (content_length) {
    uint64_t length;
    return StringUtil::atoul(content_length->value().c_str(), length) &&
           length >= config_->minimumLength();
  }

  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  return (transfer_encoding &&
          StringUtil::findToken(transfer_encoding->value().c_str(), ",",
                                Http::Headers::get().TransferEncodingValues.Chunked.c_str()));
}

bool GzipFilter::isTransferEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  if (transfer_encoding) {
    for (auto header_value :
         StringUtil::splitToken(transfer_encoding->value().c_str(), ",", true)) {
      const auto trimmed_value = StringUtil::trim(header_value);
      if (trimmed_value == Http::Headers::get().TransferEncodingValues.Gzip ||
          trimmed_value == Http::Headers::get().TransferEncodingValues.Deflate) {
        return false;
      }
    }
  }
  return true;
}

void GzipFilter::insertVaryHeader(HeaderMap& headers) {
  const Http::HeaderEntry* vary = headers.Vary();
  if (vary) {
    std::string header_values;
    if (!StringUtil::findToken(vary->value().c_str(), ",",
                               Http::Headers::get().VaryValues.AcceptEncoding, true)) {
      header_values.assign(Http::Headers::get().VaryValues.AcceptEncoding);
    }

    for (const auto value : StringUtil::splitToken(vary->value().c_str(), ",", true)) {
      const auto trimmed_value = StringUtil::trim(value);
      if (trimmed_value != Http::Headers::get().VaryValues.Wildcard) {
        header_values.empty() ? header_values.assign(trimmed_value.data(), trimmed_value.size())
                              : header_values.append(", " + std::string{trimmed_value});
      }
    }

    headers.insertVary().value(header_values);
  } else {
    headers.insertVary().value(Http::Headers::get().VaryValues.AcceptEncoding);
  }
}

} // namespace Http
} // namespace Envoy
