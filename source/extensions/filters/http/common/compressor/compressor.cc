#include "extensions/filters/http/common/compressor/compressor.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Compressors {

namespace {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_encoding_handle(Http::CustomHeaders::get().AcceptEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    cache_control_handle(Http::CustomHeaders::get().CacheControl);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    content_encoding_handle(Http::CustomHeaders::get().ContentEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    etag_handle(Http::CustomHeaders::get().Etag);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    vary_handle(Http::CustomHeaders::get().Vary);

// Default minimum length of an upstream response that allows compression.
const uint64_t DefaultMinimumContentLength = 30;

// Default content types will be used if any is provided by the user.
const std::vector<std::string>& defaultContentEncoding() {
  CONSTRUCT_ON_FIRST_USE(
      std::vector<std::string>,
      {"text/html", "text/plain", "text/css", "application/javascript", "application/x-javascript",
       "text/javascript", "text/x-javascript", "text/ecmascript", "text/js", "text/jscript",
       "text/x-js", "application/ecmascript", "application/x-json", "application/xml",
       "application/json", "image/svg+xml", "text/xml", "application/xhtml+xml"});
}

// List of CompressorFilterConfig objects registered for a stream.
struct CompressorRegistry : public StreamInfo::FilterState::Object {
  std::list<CompressorFilterConfigSharedPtr> filter_configs_;
};

// Key to per stream CompressorRegistry objects.
const std::string& compressorRegistryKey() { CONSTRUCT_ON_FIRST_USE(std::string, "compressors"); }

} // namespace

CompressorFilterConfig::CompressorFilterConfig(
    const envoy::extensions::filters::http::compressor::v3::Compressor& compressor,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    const std::string& content_encoding)
    : content_length_(contentLengthUint(compressor.content_length().value())),
      content_type_values_(contentTypeSet(compressor.content_type())),
      disable_on_etag_header_(compressor.disable_on_etag_header()),
      remove_accept_encoding_header_(compressor.remove_accept_encoding_header()),
      stats_(generateStats(stats_prefix, scope)), enabled_(compressor.runtime_enabled(), runtime),
      content_encoding_(content_encoding) {}

StringUtil::CaseUnorderedSet
CompressorFilterConfig::contentTypeSet(const Protobuf::RepeatedPtrField<std::string>& types) {
  const auto& default_content_encodings = defaultContentEncoding();
  return types.empty() ? StringUtil::CaseUnorderedSet(default_content_encodings.begin(),
                                                      default_content_encodings.end())
                       : StringUtil::CaseUnorderedSet(types.cbegin(), types.cend());
}

uint32_t CompressorFilterConfig::contentLengthUint(Protobuf::uint32 length) {
  return length > 0 ? length : DefaultMinimumContentLength;
}

CompressorFilter::CompressorFilter(const CompressorFilterConfigSharedPtr config)
    : skip_compression_{true}, config_(std::move(config)) {}

Http::FilterHeadersStatus CompressorFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const Http::HeaderEntry* accept_encoding = headers.getInline(accept_encoding_handle.handle());
  if (accept_encoding != nullptr) {
    // Capture the value of the "Accept-Encoding" request header to use it later when making
    // decision on compressing the corresponding HTTP response.
    accept_encoding_ = std::make_unique<std::string>(accept_encoding->value().getStringView());
  }

  if (config_->enabled() && config_->removeAcceptEncodingHeader()) {
    headers.removeInline(accept_encoding_handle.handle());
  }

  return Http::FilterHeadersStatus::Continue;
}

void CompressorFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;

  absl::string_view key = compressorRegistryKey();
  // To properly handle the cases where the decision on instantiating a compressor depends on
  // the presence of other compression filters in the chain the filters need to be aware of each
  // other. This is achieved by exploiting per-request data objects StreamInfo::FilterState: upon
  // setting up a CompressorFilter, the new instance registers itself in the filter state. Then in
  // the method isAcceptEncodingAllowed() the first filter is making a decision which encoder needs
  // to be used for a request, with e.g. "Accept-Encoding: deflate;q=0.75, gzip;q=0.5", and caches
  // it in the state. All other compression filters in the sequence use the cached decision.
  const StreamInfo::FilterStateSharedPtr& filter_state = callbacks.streamInfo().filterState();
  if (filter_state->hasData<CompressorRegistry>(key)) {
    CompressorRegistry& registry = filter_state->getDataMutable<CompressorRegistry>(key);
    registry.filter_configs_.push_back(config_);
  } else {
    auto registry_ptr = std::make_unique<CompressorRegistry>();
    registry_ptr->filter_configs_.push_back(config_);
    filter_state->setData(key, std::move(registry_ptr),
                          StreamInfo::FilterState::StateType::Mutable);
  }
}

Http::FilterHeadersStatus CompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                          bool end_stream) {
  const bool isEnabledAndContentLengthBigEnough =
      config_->enabled() && isMinimumContentLength(headers);
  const bool isCompressible = isEnabledAndContentLengthBigEnough && isContentTypeAllowed(headers) &&
                              !hasCacheControlNoTransform(headers) && isEtagAllowed(headers) &&
                              !headers.getInline(content_encoding_handle.handle());
  if (!end_stream && isEnabledAndContentLengthBigEnough && isAcceptEncodingAllowed(headers) &&
      isCompressible && isTransferEncodingAllowed(headers)) {
    skip_compression_ = false;
    sanitizeEtagHeader(headers);
    headers.removeContentLength();
    headers.setInline(content_encoding_handle.handle(), config_->contentEncoding());
    config_->stats().compressed_.inc();
    // Finally instantiate the compressor.
    compressor_ = config_->makeCompressor();
  } else {
    config_->stats().not_compressed_.inc();
  }

  // Even if we decided not to compress due to incompatible Accept-Encoding value,
  // the Vary header would need to be inserted to let a caching proxy in front of Envoy
  // know that the requested resource still can be served with compression applied.
  if (isCompressible) {
    insertVaryHeader(headers);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CompressorFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!skip_compression_) {
    config_->stats().total_uncompressed_bytes_.add(data.length());
    compressor_->compress(data, end_stream ? Envoy::Compression::Compressor::State::Finish
                                           : Envoy::Compression::Compressor::State::Flush);
    config_->stats().total_compressed_bytes_.add(data.length());
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus CompressorFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (!skip_compression_) {
    Buffer::OwnedImpl empty_buffer;
    compressor_->compress(empty_buffer, Envoy::Compression::Compressor::State::Finish);
    config_->stats().total_compressed_bytes_.add(empty_buffer.length());
    encoder_callbacks_->addEncodedData(empty_buffer, true);
  }
  return Http::FilterTrailersStatus::Continue;
}

bool CompressorFilter::hasCacheControlNoTransform(Http::ResponseHeaderMap& headers) const {
  const Http::HeaderEntry* cache_control = headers.getInline(cache_control_handle.handle());
  if (cache_control) {
    return StringUtil::caseFindToken(cache_control->value().getStringView(), ",",
                                     Http::CustomHeaders::get().CacheControlValues.NoTransform);
  }

  return false;
}

// This function makes decision on which encoding to use for the response body and is
// supposed to be called only once per request even if there are multiple compressor
// filters in the chain. To make a decision the function needs to know what's the
// request's Accept-Encoding, the response's Content-Type and the list of compressor
// filters in the current chain.
// TODO(rojkov): add an explicit fuzzer for chooseEncoding().
std::unique_ptr<CompressorFilter::EncodingDecision>
CompressorFilter::chooseEncoding(const Http::ResponseHeaderMap& headers) const {
  using EncPair = std::pair<absl::string_view, float>; // pair of {encoding, q_value}
  std::vector<EncPair> pairs;
  absl::string_view content_type_value;

  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr) {
    content_type_value =
        StringUtil::trim(StringUtil::cropRight(content_type->value().getStringView(), ";"));
  }

  // Find all compressors enabled for the filter chain.
  std::map<std::string, uint32_t> allowed_compressors;
  uint32_t registration_count{0};
  for (const auto& filter_config :
       decoder_callbacks_->streamInfo()
           .filterState()
           ->getDataReadOnly<CompressorRegistry>(compressorRegistryKey())
           .filter_configs_) {
    // A compressor filter may be limited to compress certain Content-Types. If the response's
    // content type doesn't match the list of content types this filter is enabled for then
    // it must be excluded from the decision process.
    // For example, there are two compressor filters in the chain e.g. "gzip" and "deflate".
    // "gzip" is configured to compress only "text/html" and "deflate" is configured to compress
    // only "application/javascript". Then comes a request with Accept-Encoding header
    // "gzip;q=1,deflate;q=.5". The corresponding response content type is "application/javascript".
    // If "gzip" is not excluded from the decision process then it will take precedence over
    // "deflate" and the resulting response won't be compressed at all.
    if (!content_type_value.empty() && !filter_config->contentTypeValues().empty()) {
      auto iter = filter_config->contentTypeValues().find(content_type_value);
      if (iter == filter_config->contentTypeValues().end()) {
        // Skip adding this filter to the list of allowed compressors.
        continue;
      }
    }

    // There could be many compressors registered for the same content encoding, e.g. consider a
    // case when there are two gzip filters using different compression levels for different content
    // sizes. In such case we ignore duplicates (or different filters for the same encoding)
    // registered last.
    auto enc = allowed_compressors.find(filter_config->contentEncoding());
    if (enc == allowed_compressors.end()) {
      allowed_compressors.insert({filter_config->contentEncoding(), registration_count});
      ++registration_count;
    }
  }

  // Find all encodings accepted by the user agent and adjust the list of allowed compressors.
  for (const auto& token : StringUtil::splitToken(*accept_encoding_, ",", false /* keep_empty */)) {
    EncPair pair =
        std::make_pair(StringUtil::trim(StringUtil::cropRight(token, ";")), static_cast<float>(1));
    const auto params = StringUtil::cropLeft(token, ";");
    if (params != token) {
      const auto q_value = StringUtil::cropLeft(params, "=");
      if (q_value != params &&
          absl::EqualsIgnoreCase("q", StringUtil::trim(StringUtil::cropRight(params, "=")))) {
        auto result = absl::SimpleAtof(StringUtil::trim(q_value), &pair.second);
        if (!result) {
          // Skip not parseable q-value.
          continue;
        }
      }
    }

    pairs.push_back(pair);

    if (!pair.second) {
      // Disallow compressors with "q=0".
      // The reason why we add encodings to "pairs" even with "q=0" is that "pairs" contains
      // client's expectations and "allowed_compressors" is what Envoy can handle. Consider
      // the cases of "Accept-Encoding: gzip;q=0, deflate, *" and "Accept-Encoding: deflate, *"
      // whereas the proxy has only "gzip" configured. If we just exclude the encodings with "q=0"
      // from "pairs" then upon noticing "*" we don't know if "gzip" is acceptable by the client.
      allowed_compressors.erase(std::string(pair.first));
    }
  }

  if (pairs.empty() || allowed_compressors.empty()) {
    // If there's no intersection between accepted encodings and the ones provided by the allowed
    // compressors, then only the "identity" encoding is acceptable.
    return std::make_unique<CompressorFilter::EncodingDecision>(
        Http::CustomHeaders::get().AcceptEncodingValues.Identity,
        CompressorFilter::EncodingDecision::HeaderStat::NotValid);
  }

  // Find intersection of encodings accepted by the user agent and provided
  // by the allowed compressors and choose the one with the highest q-value.
  EncPair choice{Http::CustomHeaders::get().AcceptEncodingValues.Identity, static_cast<float>(0)};
  for (const auto& pair : pairs) {
    if ((pair.second > choice.second) &&
        (allowed_compressors.count(std::string(pair.first)) ||
         pair.first == Http::CustomHeaders::get().AcceptEncodingValues.Identity ||
         pair.first == Http::CustomHeaders::get().AcceptEncodingValues.Wildcard)) {
      choice = pair;
    }
  }

  if (!choice.second) {
    // The value of "Accept-Encoding" must be invalid as we ended up with zero q-value.
    return std::make_unique<CompressorFilter::EncodingDecision>(
        Http::CustomHeaders::get().AcceptEncodingValues.Identity,
        CompressorFilter::EncodingDecision::HeaderStat::NotValid);
  }

  // The "identity" encoding (no compression) is always available.
  if (choice.first == Http::CustomHeaders::get().AcceptEncodingValues.Identity) {
    return std::make_unique<CompressorFilter::EncodingDecision>(
        Http::CustomHeaders::get().AcceptEncodingValues.Identity,
        CompressorFilter::EncodingDecision::HeaderStat::Identity);
  }

  // If wildcard is given then use which ever compressor is registered first.
  if (choice.first == Http::CustomHeaders::get().AcceptEncodingValues.Wildcard) {
    auto first_registered = std::min_element(
        allowed_compressors.begin(), allowed_compressors.end(),
        [](const std::pair<std::string, uint32_t>& a,
           const std::pair<std::string, uint32_t>& b) -> bool { return a.second < b.second; });
    return std::make_unique<CompressorFilter::EncodingDecision>(
        first_registered->first, CompressorFilter::EncodingDecision::HeaderStat::Wildcard);
  }

  return std::make_unique<CompressorFilter::EncodingDecision>(
      std::string(choice.first), CompressorFilter::EncodingDecision::HeaderStat::ValidCompressor);
}

// Check if this filter was chosen to compress. Also update the filter's stat counters related to
// the Accept-Encoding header.
bool CompressorFilter::shouldCompress(const CompressorFilter::EncodingDecision& decision) const {
  const bool should_compress =
      absl::EqualsIgnoreCase(config_->contentEncoding(), decision.encoding());

  switch (decision.stat()) {
  case CompressorFilter::EncodingDecision::HeaderStat::ValidCompressor:
    if (should_compress) {
      config_->stats().header_compressor_used_.inc();
      // TODO(rojkov): Remove this increment when the gzip-specific stat is gone.
      if (absl::EqualsIgnoreCase("gzip", config_->contentEncoding())) {
        config_->stats().header_gzip_.inc();
      }
    } else {
      // Some other compressor filter in the same chain compressed the response body,
      // but not this filter.
      config_->stats().header_compressor_overshadowed_.inc();
    }
    break;
  case CompressorFilter::EncodingDecision::HeaderStat::Identity:
    config_->stats().header_identity_.inc();
    break;
  case CompressorFilter::EncodingDecision::HeaderStat::Wildcard:
    config_->stats().header_wildcard_.inc();
    break;
  default:
    config_->stats().header_not_valid_.inc();
    break;
  }

  return should_compress;
}

bool CompressorFilter::isAcceptEncodingAllowed(const Http::ResponseHeaderMap& headers) const {
  if (accept_encoding_ == nullptr) {
    config_->stats().no_accept_header_.inc();
    return false;
  }

  const absl::string_view encoding_decision_key{"encoding_decision"};

  // Check if we have already cached our decision on encoding.
  const StreamInfo::FilterStateSharedPtr& filter_state =
      decoder_callbacks_->streamInfo().filterState();
  if (filter_state->hasData<CompressorFilter::EncodingDecision>(encoding_decision_key)) {
    const CompressorFilter::EncodingDecision& decision =
        filter_state->getDataReadOnly<CompressorFilter::EncodingDecision>(encoding_decision_key);
    return shouldCompress(decision);
  }

  // No cached decision found, so decide now.
  std::unique_ptr<CompressorFilter::EncodingDecision> decision = chooseEncoding(headers);
  bool result = shouldCompress(*decision);
  filter_state->setData(encoding_decision_key, std::move(decision),
                        StreamInfo::FilterState::StateType::ReadOnly);
  return result;
}

bool CompressorFilter::isContentTypeAllowed(Http::ResponseHeaderMap& headers) const {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr && !config_->contentTypeValues().empty()) {
    const absl::string_view value =
        StringUtil::trim(StringUtil::cropRight(content_type->value().getStringView(), ";"));
    return config_->contentTypeValues().find(value) != config_->contentTypeValues().end();
  }

  return true;
}

bool CompressorFilter::isEtagAllowed(Http::ResponseHeaderMap& headers) const {
  const bool is_etag_allowed =
      !(config_->disableOnEtagHeader() && headers.getInline(etag_handle.handle()));
  if (!is_etag_allowed) {
    config_->stats().not_compressed_etag_.inc();
  }
  return is_etag_allowed;
}

bool CompressorFilter::isMinimumContentLength(Http::ResponseHeaderMap& headers) const {
  const Http::HeaderEntry* content_length = headers.ContentLength();
  if (content_length != nullptr) {
    uint64_t length;
    const bool is_minimum_content_length =
        absl::SimpleAtoi(content_length->value().getStringView(), &length) &&
        length >= config_->minimumLength();
    if (!is_minimum_content_length) {
      config_->stats().content_length_too_small_.inc();
    }
    return is_minimum_content_length;
  }

  return StringUtil::caseFindToken(headers.getTransferEncodingValue(), ",",
                                   Http::Headers::get().TransferEncodingValues.Chunked);
}

bool CompressorFilter::isTransferEncodingAllowed(Http::ResponseHeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  if (transfer_encoding != nullptr) {
    for (absl::string_view header_value :
         StringUtil::splitToken(transfer_encoding->value().getStringView(), ",", true)) {
      const auto trimmed_value = StringUtil::trim(header_value);
      if (absl::EqualsIgnoreCase(trimmed_value, config_->contentEncoding()) ||
          // or any other compression type known to Envoy
          absl::EqualsIgnoreCase(trimmed_value, Http::Headers::get().TransferEncodingValues.Gzip) ||
          absl::EqualsIgnoreCase(trimmed_value,
                                 Http::Headers::get().TransferEncodingValues.Deflate)) {
        return false;
      }
    }
  }

  return true;
}

void CompressorFilter::insertVaryHeader(Http::ResponseHeaderMap& headers) {
  const Http::HeaderEntry* vary = headers.getInline(vary_handle.handle());
  if (vary != nullptr) {
    if (!StringUtil::findToken(vary->value().getStringView(), ",",
                               Http::CustomHeaders::get().VaryValues.AcceptEncoding, true)) {
      std::string new_header;
      absl::StrAppend(&new_header, vary->value().getStringView(), ", ",
                      Http::CustomHeaders::get().VaryValues.AcceptEncoding);
      headers.setInline(vary_handle.handle(), new_header);
    }
  } else {
    headers.setReferenceInline(vary_handle.handle(),
                               Http::CustomHeaders::get().VaryValues.AcceptEncoding);
  }
}

// TODO(gsagula): It seems that every proxy has a different opinion how to handle Etag. Some
// discussions around this topic have been going on for over a decade, e.g.,
// https://bz.apache.org/bugzilla/show_bug.cgi?id=45023
// This design attempts to stay more on the safe side by preserving weak etags and removing
// the strong ones when disable_on_etag_header is false. Envoy does NOT re-write entity tags.
void CompressorFilter::sanitizeEtagHeader(Http::ResponseHeaderMap& headers) {
  const Http::HeaderEntry* etag = headers.getInline(etag_handle.handle());
  if (etag != nullptr) {
    absl::string_view value(etag->value().getStringView());
    if (value.length() > 2 && !((value[0] == 'w' || value[0] == 'W') && value[1] == '/')) {
      headers.removeInline(etag_handle.handle());
    }
  }
}

} // namespace Compressors
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
