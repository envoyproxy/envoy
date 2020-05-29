#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
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
    : stats_prefix_(fmt::format("{}decompressor.{}.{}", stats_prefix,
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
      decompression_enabled_(proto_config.enabled(), runtime) {}

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
  if (config_->responseDirectionConfig().decompressionEnabled() &&
      config_->requestDirectionConfig().advertiseAcceptEncoding()) {
    headers.appendAcceptEncoding(config_->contentEncoding(), ",");
    ENVOY_STREAM_LOG(debug,
                     "DecompressorFilter::decodeHeaders advertise Accept-Encoding with value '{}'",
                     *decoder_callbacks_, headers.AcceptEncoding()->value().getStringView());
  }

  //   2. If request decompression is enabled, then decompress the request.
  return maybeInitDecompress(config_->requestDirectionConfig(), request_decompressor_,
                             *decoder_callbacks_, headers);
};

Http::FilterDataStatus DecompressorFilter::decodeData(Buffer::Instance& data, bool) {
  return maybeDecompress(config_->requestDirectionConfig(), request_decompressor_,
                         *decoder_callbacks_, data);
}

Http::FilterHeadersStatus DecompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                            bool end_stream) {
  // Headers only response, continue.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "DecompressorFilter::encodeHeaders: {}", *encoder_callbacks_, headers);

  return maybeInitDecompress(config_->responseDirectionConfig(), response_decompressor_,
                             *encoder_callbacks_, headers);
}

Http::FilterDataStatus DecompressorFilter::encodeData(Buffer::Instance& data, bool) {
  return maybeDecompress(config_->responseDirectionConfig(), response_decompressor_,
                         *encoder_callbacks_, data);
}

Http::FilterHeadersStatus DecompressorFilter::maybeInitDecompress(
    const DecompressorFilterConfig::DirectionConfig& direction_config,
    Compression::Decompressor::DecompressorPtr& decompressor,
    Http::StreamFilterCallbacks& callbacks, Http::RequestOrResponseHeaderMap& headers) {
  if (direction_config.decompressionEnabled() && !hasCacheControlNoTransform(headers) &&
      contentEncodingMatches(headers)) {
    direction_config.stats().decompressed_.inc();
    decompressor = config_->makeDecompressor();

    // Update headers.
    headers.removeContentLength();
    modifyContentEncoding(headers);

    ENVOY_STREAM_LOG(debug, "do decompress {}: {}", callbacks, direction_config.logString(),
                     headers);
  } else {
    direction_config.stats().not_decompressed_.inc();
    ENVOY_STREAM_LOG(debug, "do not decompress {}: {}", callbacks, direction_config.logString(),
                     headers);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DecompressorFilter::maybeDecompress(
    const DecompressorFilterConfig::DirectionConfig& direction_config,
    const Compression::Decompressor::DecompressorPtr& decompressor,
    Http::StreamFilterCallbacks& callbacks, Buffer::Instance& input_buffer) const {
  if (decompressor) {
    Buffer::OwnedImpl output_buffer;
    decompressor->decompress(input_buffer, output_buffer);

    // Report decompression via stats and logging before modifying the input buffer.
    direction_config.stats().total_compressed_bytes_.add(input_buffer.length());
    direction_config.stats().total_uncompressed_bytes_.add(output_buffer.length());
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

/**
 * Content-Encoding matches if the configured encoding is the first value in the comma-delimited
 * Content-Encoding header, regardless of spacing and casing.
 */
bool DecompressorFilter::contentEncodingMatches(Http::RequestOrResponseHeaderMap& headers) const {
  if (headers.ContentEncoding()) {
    absl::string_view coding = StringUtil::trim(
        StringUtil::cropRight(headers.ContentEncoding()->value().getStringView(), ","));
    return StringUtil::CaseInsensitiveCompare()(config_->contentEncoding(), coding);
  }
  return false;
}

void DecompressorFilter::modifyContentEncoding(Http::RequestOrResponseHeaderMap& headers) const {
  const auto all_codings = StringUtil::trim(headers.ContentEncoding()->value().getStringView());
  const auto remaining_codings = StringUtil::trim(StringUtil::cropLeft(all_codings, ","));

  if (remaining_codings != all_codings) {
    headers.setContentEncoding(remaining_codings);
  } else {
    headers.removeContentEncoding();
  }
}

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy