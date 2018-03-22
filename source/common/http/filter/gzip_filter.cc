#include "common/http/filter/gzip_filter.h"

#include "common/common/macros.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

namespace {
// Default zlib memory level.
const uint64_t DefaultMemoryLevel = 5;

// Default and maximum compression window size.
const uint64_t DefaultWindowBits = 12;

// Minimum length of an upstream response that allows compression.
const uint64_t MinimumContentLength = 30;

// When summed to window bits, this sets a gzip header and trailer around the compressed data.
const uint64_t GzipHeaderValue = 16;

// Used for verifying accept-encoding values.
const char ZeroQvalueString[] = "q=0";

// Default content types will be used if any is provided by the user.
const std::vector<std::string>& defaultContentEncoding() {
  CONSTRUCT_ON_FIRST_USE(std::vector<std::string>,
                         {"text/html", "text/plain", "text/css", "application/javascript",
                          "application/json", "image/svg+xml", "text/xml",
                          "application/xhtml+xml"});
}

} // namespace

GzipFilterConfig::GzipFilterConfig(const envoy::config::filter::http::gzip::v2::Gzip& gzip)
    : compression_level_(compressionLevelEnum(gzip.compression_level())),
      compression_strategy_(compressionStrategyEnum(gzip.compression_strategy())),
      content_length_(contentLengthUint(gzip.content_length().value())),
      memory_level_(memoryLevelUint(gzip.memory_level().value())),
      window_bits_(windowBitsUint(gzip.window_bits().value())),
      content_type_values_(contentTypeSet(gzip.content_type())),
      disable_on_etag_header_(gzip.disable_on_etag_header()),
      remove_accept_encoding_header_(gzip.remove_accept_encoding_header()) {}

Compressor::ZlibCompressorImpl::CompressionLevel GzipFilterConfig::compressionLevelEnum(
    envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum compression_level) {
  switch (compression_level) {
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum::
      Gzip_CompressionLevel_Enum_BEST:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Best;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionLevel_Enum::
      Gzip_CompressionLevel_Enum_SPEED:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Speed;
  default:
    return Compressor::ZlibCompressorImpl::CompressionLevel::Standard;
  }
}

Compressor::ZlibCompressorImpl::CompressionStrategy GzipFilterConfig::compressionStrategyEnum(
    envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy compression_strategy) {
  switch (compression_strategy) {
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_RLE:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Rle;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_FILTERED:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Filtered;
  case envoy::config::filter::http::gzip::v2::Gzip_CompressionStrategy::
      Gzip_CompressionStrategy_HUFFMAN:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Huffman;
  default:
    return Compressor::ZlibCompressorImpl::CompressionStrategy::Standard;
  }
}

StringUtil::CaseUnorderedSet GzipFilterConfig::contentTypeSet(
    const Protobuf::RepeatedPtrField<Envoy::ProtobufTypes::String>& types) {
  return types.empty() ? StringUtil::CaseUnorderedSet(defaultContentEncoding().begin(),
                                                      defaultContentEncoding().end())
                       : StringUtil::CaseUnorderedSet(types.cbegin(), types.cend());
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
  if (isAcceptEncodingAllowed(headers)) {
    skip_compression_ = false;
    if (config_->removeAcceptEncodingHeader()) {
      headers.removeAcceptEncoding();
    }
  }

  return FilterHeadersStatus::Continue;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (!end_stream && !skip_compression_ && isMinimumContentLength(headers) &&
      isContentTypeAllowed(headers) && !hasCacheControlNoTransform(headers) &&
      isEtagAllowed(headers) && isTransferEncodingAllowed(headers) && !headers.ContentEncoding()) {
    sanitizeEtagHeader(headers);
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

  const uint64_t n_data = data.length();

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

bool GzipFilter::hasCacheControlNoTransform(HeaderMap& headers) const {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  if (cache_control) {
    return StringUtil::caseFindToken(cache_control->value().c_str(), ",",
                                     Http::Headers::get().CacheControlValues.NoTransform.c_str());
  }

  return false;
}

// TODO(gsagula): Since gzip is the only available content-encoding in Envoy at the moment,
// order/priority of preferred server encodings is disregarded (RFC2616-14.3). Replace this
// with a data structure that parses Accept-Encoding values and allows fast lookup of
// key/priority. Also, this should be part of some utility library.
// https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html
bool GzipFilter::isAcceptEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* accept_encoding = headers.AcceptEncoding();

  if (accept_encoding) {
    bool is_wildcard = false; // true if found and not followed by `q=0`.
    for (const auto token : StringUtil::splitToken(headers.AcceptEncoding()->value().c_str(), ",",
                                                   false /* keep_empty */)) {
      const auto value = StringUtil::trim(StringUtil::cropRight(token, ";"));
      const auto q_value = StringUtil::trim(StringUtil::cropLeft(token, ";"));
      // If value is the gzip coding, check the qvalue and return.
      if (value == Http::Headers::get().AcceptEncodingValues.Gzip) {
        return !StringUtil::caseCompare(q_value, ZeroQvalueString);
      }
      // If value is the identity coding, just check the qvalue and return.
      if (value == Http::Headers::get().AcceptEncodingValues.Identity) {
        return !StringUtil::caseCompare(q_value, ZeroQvalueString);
      }
      if (value == Http::Headers::get().AcceptEncodingValues.Wildcard) {
        is_wildcard = !StringUtil::caseCompare(q_value, ZeroQvalueString);
      }
    }
    // If neither identity nor gzip codings are present, we return the wildcard.
    return is_wildcard;
  }
  // If no accept-encoding header is present, return false.
  return false;
}

bool GzipFilter::isContentTypeAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type && !config_->contentTypeValues().empty()) {
    std::string value{StringUtil::trim(StringUtil::cropRight(content_type->value().c_str(), ";"))};
    return config_->contentTypeValues().find(value) != config_->contentTypeValues().end();
  }

  return true;
}

bool GzipFilter::isEtagAllowed(HeaderMap& headers) const {
  return !(config_->disableOnEtagHeader() && headers.Etag());
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
          StringUtil::caseFindToken(transfer_encoding->value().c_str(), ",",
                                    Http::Headers::get().TransferEncodingValues.Chunked.c_str()));
}

bool GzipFilter::isTransferEncodingAllowed(HeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  if (transfer_encoding) {
    for (auto header_value :
         // TODO(gsagula): add HeaderMap::string_view() so string length doesn't need to be computed
         // twice. Find all other sites where this can be improved.
         StringUtil::splitToken(transfer_encoding->value().c_str(), ",", true)) {
      const auto trimmed_value = StringUtil::trim(header_value);
      if (StringUtil::caseCompare(trimmed_value,
                                  Http::Headers::get().TransferEncodingValues.Gzip) ||
          StringUtil::caseCompare(trimmed_value,
                                  Http::Headers::get().TransferEncodingValues.Deflate)) {
        return false;
      }
    }
  }

  return true;
}

void GzipFilter::insertVaryHeader(HeaderMap& headers) {
  const Http::HeaderEntry* vary = headers.Vary();
  if (vary) {
    if (!StringUtil::findToken(vary->value().c_str(), ",",
                               Http::Headers::get().VaryValues.AcceptEncoding, true)) {
      std::string new_header;
      absl::StrAppend(&new_header, vary->value().c_str(), ", ",
                      Http::Headers::get().VaryValues.AcceptEncoding);
      headers.insertVary().value(new_header);
    }
  } else {
    headers.insertVary().value(Http::Headers::get().VaryValues.AcceptEncoding);
  }
}

// TODO(gsagula): It seems that every proxy has a different opinion how to handle Etag. Some
// discussions around this topic have been going on for over a decade, e.g.,
// https://bz.apache.org/bugzilla/show_bug.cgi?id=45023
// This design attempts to stay more on the safe side by preserving weak etags and removing
// the strong ones when disable_on_etag_header is false. Envoy does NOT re-write entity tags.
void GzipFilter::sanitizeEtagHeader(HeaderMap& headers) {
  const Http::HeaderEntry* etag = headers.Etag();
  if (etag) {
    absl::string_view value(etag->value().c_str());
    if (value.length() > 2 && !((value[0] == 'w' || value[0] == 'W') && value[1] == '/')) {
      headers.removeEtag();
    }
  }
}

} // namespace Http
} // namespace Envoy
