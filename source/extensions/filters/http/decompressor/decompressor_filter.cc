#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/macros.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

DecompressorFilterConfig::DecompressorFilterConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Compression::Decompressor::DecompressorFactoryPtr decompressor_factory)
    : stats_prefix_(fmt::format("{}decompressor.{}{}", stats_prefix,
                                proto_config.decompressor_library().name() +
                                            proto_config.decompressor_library().name() ==
                                        ""
                                    ? ""
                                    : ".",
                                decompressor_factory->statsPrefix() + ".")),
      decompressor_factory_(std::move(decompressor_factory)),
      request_direction_config_(proto_config.request_direction_config(), stats_prefix_, scope,
                                runtime),
      response_direction_config_(proto_config.response_direction_config(), stats_prefix_, scope,
                                 runtime) {}

DecompressorFilterConfig::DirectionConfig::DirectionConfig(
    const bool is_request_direction,
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::CommonDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : request_or_response_(is_request_direction ? "request" : "response"),
      stats_(generateStats(stats_prefix, scope)),
      decompression_enabled_(proto_config.enabled(), runtime) {}

DecompressorFilterConfig::RequestDirectionConfig::RequestDirectionConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::RequestDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(true, proto_config.common_config(), stats_prefix + "request.", scope,
                      runtime),
      advertise_accept_encoding_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, advertise_accept_encoding, true)) {}

DecompressorFilterConfig::ResponseDirectionConfig::ResponseDirectionConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::ResponseDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(false, proto_config.common_config(), stats_prefix + "response.", scope,
                      runtime) {}

DecompressorFilter::DecompressorFilter(DecompressorFilterConfigSharedPtr config)
    : config_(std::move(config)) {}

Http::FilterHeadersStatus DecompressorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                            bool end_stream) {
  // Headers only request, continue.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "DecompressorFilter::decodeHeaders: {}", *decoder_callbacks_, headers);

  // Two responsibilities on the request side:
  //   1. If request decompression is enabled, then decompress the request.
  //   2. If response decompression is enabled (and advertisement is enabled?), then advertise to
  //   the upstream that this hop is able to decompress responses via the AcceptEncoding header.
  maybeInitDecompress(config_->requestDirectionConfig(), request_decompressor_, *decoder_callbacks_,
                      headers);

  if (config_->responseDirectionConfig().decompressionEnabled()) {
    // FIX ME: discuss this unconditional addition. Should this be optional and
    // configurable? i.e even if response decompression is enabled the filter could chose to
    // advertise or not.
    injectAcceptEncoding(headers);
    ENVOY_STREAM_LOG(debug,
                     "DecompressorFilter::decodeHeaders advertise Accept-Encoding with value '{}'",
                     *decoder_callbacks_, headers.AcceptEncoding()->value().getStringView());
  }

  return Http::FilterHeadersStatus::Continue;
};

Http::FilterDataStatus DecompressorFilter::decodeData(Buffer::Instance& data, bool) {
  return maybeDecompress(config_->requestDirectionConfig(), request_decompressor_.get(),
                         *decoder_callbacks_, data);
}

Http::FilterHeadersStatus DecompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                            bool end_stream) {
  // Headers only response, continue.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "DecompressorFilter::encodeHeaders: {}", *encoder_callbacks_, headers);

  maybeInitDecompress(config_->responseDirectionConfig(), response_decompressor_,
                      *encoder_callbacks_, headers);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DecompressorFilter::encodeData(Buffer::Instance& data, bool) {
  return maybeDecompress(config_->requestDirectionConfig(), response_decompressor_.get(),
                         *decoder_callbacks_, data);
}

void DecompressorFilter::maybeInitDecompress(
    const DecompressorFilterConfig::DirectionConfig& direction_config,
    Compression::Decompressor::DecompressorPtr& decompressor,
    Http::StreamFilterCallbacks& callbacks, Http::RequestOrResponseHeaderMap& headers) {
  if (direction_config.decompressionEnabled() && !hasCacheControlNoTransform(headers) &&
      contentEncodingMatches(headers)) {
    direction_config.stats().decompressed_.inc();
    decompressor = config_->makeDecompressor();

    // FIX ME: if there is length present the filter will need to buffer, and decompress
    // only once all the data has been received. The alternative that was implemented in the
    // original branch was to delete Content Length and add Transfer Encoding chunked. This will
    // most likely not work at Lyft. Moreover, is that acceptable for all decompression schemes?
    // Lastly, the code was different in the request (absent) and response path, why?
    headers.removeContentLength();
    sanitizeTransferEncoding(headers);

    removeContentEncoding(headers);
    // Note that the log will print the updated headers. The incoming headers can be seen from the
    // log above.
    ENVOY_STREAM_LOG(debug, "do decompress {}: {}", callbacks, direction_config.logString(),
                     headers);
  } else {
    ENVOY_STREAM_LOG(debug, "do not decompress {}: {}", callbacks, direction_config.logString(),
                     headers);
    direction_config.stats().not_decompressed_.inc();
  }
}

Http::FilterDataStatus DecompressorFilter::maybeDecompress(
    const DecompressorFilterConfig::DirectionConfig& direction_config,
    Compression::Decompressor::Decompressor* decompressor, Http::StreamFilterCallbacks& callbacks,
    Buffer::Instance& input_buffer) const {
  if (decompressor) {
    // FIX ME: do we want stats on total bytes like the compressor?
    Buffer::OwnedImpl output_buffer;
    decompressor->decompress(input_buffer, output_buffer);
    ENVOY_STREAM_LOG(debug, "{} data decompressed from {} bytes to {} bytes", callbacks,
                     direction_config.logString(), input_buffer.length(), output_buffer.length());
    input_buffer.drain(input_buffer.length());
    input_buffer.add(output_buffer);
  }
  return Http::FilterDataStatus::Continue;
}

bool DecompressorFilter::hasCacheControlNoTransform(
    Http::RequestOrResponseHeaderMap& headers) const {
  return headers.CacheControl()
             ? StringUtil::caseFindToken(headers.CacheControl()->value().getStringView(), ",",
                                         Http::Headers::get().CacheControlValues.NoTransform)
             : false;
}

bool DecompressorFilter::contentEncodingMatches(Http::RequestOrResponseHeaderMap& headers) const {
  if (headers.ContentEncoding()) {
    // FIX ME: is there no way to get the first value in the HeaderString?
    absl::string_view coding = StringUtil::trim(
        StringUtil::cropRight(headers.ContentEncoding()->value().getStringView(), ","));
    if (StringUtil::CaseInsensitiveCompare()(config_->contentEncoding(), coding)) {
      return true;
    }
  }
  return false;
}

void DecompressorFilter::removeContentEncoding(Http::RequestOrResponseHeaderMap& headers) const {
  // FIX ME: there should be a general HeaderString method to remove a value from the comma
  // delimited list.
  const auto all_codings = headers.ContentEncoding()->value().getStringView();
  const auto remaining_codings = StringUtil::trim(StringUtil::cropLeft(all_codings, ","));

  if (remaining_codings != all_codings) {
    headers.setContentEncoding(remaining_codings);
  } else {
    headers.removeContentEncoding();
  }
}

// TODO(junr03): inject encoding with configurable qvalue with q=1 by default.
void DecompressorFilter::injectAcceptEncoding(Http::RequestHeaderMap& headers) const {
  // FIX ME: the code here in the original branch prepended the current filter's
  // content encoding. However, my read of the content encoding spec leads me to think that we
  // should append. Discuss in code review.
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Encoding
  headers.appendAcceptEncoding(config_->contentEncoding(), ",");
}

/**
 * Add "chunked" to the Transfer-Encoding header if it's not there yet.
 */
void DecompressorFilter::sanitizeTransferEncoding(Http::RequestOrResponseHeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
  // FIX ME: should there be a utility for finding if a specific value is present in the
  // header value string? Is the issue that not all headers are delimited the same way?
  if (!(transfer_encoding &&
        StringUtil::caseFindToken(transfer_encoding->value().getStringView(), ",",
                                  Http::Headers::get().TransferEncodingValues.Chunked,
                                  true /* trim_whitespace */))) {
    headers.appendTransferEncoding(Http::Headers::get().TransferEncodingValues.Chunked, ",");
  }
}

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy