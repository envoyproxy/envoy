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
    : stats_prefix_(fmt::format("{}decompressor.{}.{}.", stats_prefix,
                                proto_config.decompressor_library().name(),
                                decompressor_factory->statsPrefix())),
      decompressor_factory_(std::move(decompressor_factory)),
      request_direction_config_(proto_config.request_direction_config(), stats_prefix_, scope,
                                runtime),
      response_direction_config_(proto_config.response_direction_config(), stats_prefix_, scope,
                                 runtime) {}

DecompressorFilterConfig::DirectionConfig::DirectionConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::CommonDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : stats_(generateStats(stats_prefix, scope)),
      decompression_enabled_(proto_config.enabled(), runtime),
      max_buffered_bytes_(proto_config.has_max_buffered_bytes()
                              ? absl::optional<uint32_t>(proto_config.max_buffered_bytes().value())
                              : absl::nullopt) {}

DecompressorFilterConfig::RequestDirectionConfig::RequestDirectionConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::RequestDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.common_config(), stats_prefix + "request.", scope, runtime),
      advertise_accept_encoding_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, advertise_accept_encoding, true)) {}

DecompressorFilterConfig::ResponseDirectionConfig::ResponseDirectionConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor::ResponseDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.common_config(), stats_prefix + "response.", scope, runtime) {}

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
  //   1. If response decompression is enabled (and advertisement is enabled), then advertise to
  //      the upstream that this hop is able to decompress responses via the Accept-Encoding header.
  if (config_->responseDirectionConfig().decompressionEnabled()) {
    if (config_->requestDirectionConfig().advertiseAcceptEncoding()) {
      headers.appendAcceptEncoding(config_->contentEncoding(), ",");
      ENVOY_STREAM_LOG(
          debug, "DecompressorFilter::decodeHeaders advertise Accept-Encoding with value '{}'",
          *decoder_callbacks_, headers.AcceptEncoding()->value().getStringView());
    }
  }

  //   2. If request decompression is enabled, then decompress the request.
  return maybeInitDecompress(config_->requestDirectionConfig(), request_decompressor_,
                             decoder_callbacks_, *decoder_callbacks_, headers);
};

Http::FilterDataStatus DecompressorFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  return maybeDecompress(config_->requestDirectionConfig(), request_decompressor_.get(),
                         *decoder_callbacks_, data, end_stream);
}

Http::FilterHeadersStatus DecompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                            bool end_stream) {
  // Headers only response, continue.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "DecompressorFilter::encodeHeaders: {}", *encoder_callbacks_, headers);

  return maybeInitDecompress(config_->responseDirectionConfig(), response_decompressor_,
                             encoder_callbacks_, *encoder_callbacks_, headers);
}

Http::FilterDataStatus DecompressorFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  return maybeDecompress(config_->responseDirectionConfig(), response_decompressor_.get(),
                         *encoder_callbacks_, data, end_stream);
}

Http::FilterHeadersStatus DecompressorFilter::maybeInitDecompress(
    DecompressorFilterConfig::DirectionConfig& direction_config,
    Compression::Decompressor::DecompressorPtr& decompressor,
    absl::variant<Http::StreamDecoderFilterCallbacks*, Http::StreamEncoderFilterCallbacks*>
        decoder_or_encoder_callbacks,
    Http::StreamFilterCallbacks& callbacks, Http::RequestOrResponseHeaderMap& headers) {
  if (direction_config.decompressionEnabled() && !hasCacheControlNoTransform(headers) &&
      contentEncodingMatches(headers)) {
    direction_config.stats().decompressed_.inc();
    decompressor = config_->makeDecompressor();
    // remove the compression scheme's content encoding because the filter is going to decompress.
    removeContentEncoding(headers);

    // Per the filter documentation, only buffer if there is a Content-Length header present, and
    // the filter is configured to buffer.
    if (headers.removeContentLength() > 0 && direction_config.maxBufferedBytes()) {
      direction_config.setBuffering(true);
      if (absl::holds_alternative<Http::StreamDecoderFilterCallbacks*>(
              decoder_or_encoder_callbacks)) {
        absl::get<Http::StreamDecoderFilterCallbacks*>(decoder_or_encoder_callbacks)
            ->setDecoderBufferLimit(direction_config.maxBufferedBytes().value());
      } else {
        absl::get<Http::StreamEncoderFilterCallbacks*>(decoder_or_encoder_callbacks)
            ->setEncoderBufferLimit(direction_config.maxBufferedBytes().value());
      }
      direction_config.setHeaders(&headers);
      // Note that the log will print the updated headers. The incoming headers can be seen from the
      // log in decode/encodeHeaders.
      ENVOY_STREAM_LOG(debug, "do decompress (without buffering) {}: {}", callbacks,
                       direction_config.logString(), headers);
      return Http::FilterHeadersStatus::StopIteration;
    }
    // FIX ME: chunked should not be added if we buffer, right?
    sanitizeTransferEncoding(headers);

    ENVOY_STREAM_LOG(debug, "do decompress (without buffering) {}: {}", callbacks,
                     direction_config.logString(), headers);
  } else {
    ENVOY_STREAM_LOG(debug, "do not decompress {}: {}", callbacks, direction_config.logString(),
                     headers);
    direction_config.stats().not_decompressed_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus
DecompressorFilter::maybeDecompress(DecompressorFilterConfig::DirectionConfig& direction_config,
                                    Compression::Decompressor::Decompressor* decompressor,
                                    Http::StreamFilterCallbacks& callbacks,
                                    Buffer::Instance& input_buffer, const bool end_stream) const {
  if (decompressor) {
    Buffer::OwnedImpl output_buffer;
    decompressor->decompress(input_buffer, output_buffer);
    direction_config.stats().total_compressed_bytes_.add(input_buffer.length());
    direction_config.stats().total_uncompressed_bytes_.add(output_buffer.length());
    direction_config.addToUncompressedLength(output_buffer.length());
    ENVOY_STREAM_LOG(debug, "{} data decompressed from {} bytes to {} bytes", callbacks,
                     direction_config.logString(), input_buffer.length(), output_buffer.length());
    input_buffer.drain(input_buffer.length());
    input_buffer.add(output_buffer);

    if (direction_config.buffering()) {
      if (!end_stream) {
        // The connection manager can deal with buffering. The input_buffer now contains the
        // uncompressed bytes;
        return Http::FilterDataStatus::StopIterationAndBuffer;
      }
      // In the last frame reset the Content-Length header with the uncompressed size.
      ASSERT(direction_config.headers() != nullptr);
      direction_config.headers()->setContentLength(direction_config.uncompressedLength());
    }
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
    absl::string_view coding = StringUtil::trim(
        StringUtil::cropRight(headers.ContentEncoding()->value().getStringView(), ","));
    if (StringUtil::CaseInsensitiveCompare()(config_->contentEncoding(), coding)) {
      return true;
    }
  }
  return false;
}

void DecompressorFilter::removeContentEncoding(Http::RequestOrResponseHeaderMap& headers) const {
  const auto all_codings = headers.ContentEncoding()->value().getStringView();
  const auto remaining_codings = StringUtil::trim(StringUtil::cropLeft(all_codings, ","));

  if (remaining_codings != all_codings) {
    headers.setContentEncoding(remaining_codings);
  } else {
    headers.removeContentEncoding();
  }
}

/**
 * Add "chunked" to the Transfer-Encoding header if it's not there yet.
 */
void DecompressorFilter::sanitizeTransferEncoding(Http::RequestOrResponseHeaderMap& headers) const {
  const Http::HeaderEntry* transfer_encoding = headers.TransferEncoding();
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