#include "source/extensions/filters/http/decompressor/decompressor_filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_encoding_handle(Http::CustomHeaders::get().AcceptEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    cache_control_request_handle(Http::CustomHeaders::get().CacheControl);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    content_encoding_request_handle(Http::CustomHeaders::get().ContentEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    cache_control_response_handle(Http::CustomHeaders::get().CacheControl);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    content_encoding_response_handle(Http::CustomHeaders::get().ContentEncoding);

DecompressorFilterConfig::DecompressorFilterConfig(
    const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Compression::Decompressor::DecompressorFactoryPtr decompressor_factory)
    : stats_prefix_(fmt::format("{}decompressor.{}.{}", stats_prefix,
                                proto_config.decompressor_library().name(),
                                decompressor_factory->statsPrefix())),
      trailers_prefix_(fmt::format("{}-decompressor-{}",
                                   ThreadSafeSingleton<Http::PrefixValue>::get().prefix(),
                                   proto_config.decompressor_library().name())),
      decompressor_stats_prefix_(stats_prefix_ + "decompressor_library"),
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
      ignore_no_transform_header_(proto_config.ignore_no_transform_header()) {}

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
    : config_(std::move(config)), request_byte_tracker_(config_->trailersCompressedBytesString(),
                                                        config_->trailersUncompressedBytesString()),
      response_byte_tracker_(config_->trailersCompressedBytesString(),
                             config_->trailersUncompressedBytesString()) {}

Http::FilterHeadersStatus DecompressorFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                            bool end_stream) {
  // Two responsibilities on the request side:
  //   1. If response decompression is enabled (and advertisement is enabled), then advertise to
  //      the upstream that this hop is able to decompress responses via the Accept-Encoding header.
  if (config_->responseDirectionConfig().decompressionEnabled() &&
      config_->requestDirectionConfig().advertiseAcceptEncoding()) {
    const std::string new_accept_encoding_header = Http::HeaderUtility::addEncodingToAcceptEncoding(
        headers.getInlineValue(accept_encoding_handle.handle()), config_->contentEncoding());
    headers.setInline(accept_encoding_handle.handle(), new_accept_encoding_header);

    ENVOY_STREAM_LOG(debug,
                     "DecompressorFilter::decodeHeaders advertise Accept-Encoding with value '{}'",
                     *decoder_callbacks_, headers.getInlineValue(accept_encoding_handle.handle()));
  }

  // Headers-only requests do not, by definition, get decompressed.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "DecompressorFilter::decodeHeaders: {}", *decoder_callbacks_, headers);

  //   2. Setup request decompression if all checks comply.
  return maybeInitDecompress(config_->requestDirectionConfig(), request_decompressor_,
                             *decoder_callbacks_, headers);
};

Http::FilterDataStatus DecompressorFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (request_decompressor_) {
    HeaderMapOptRef trailers;
    if (end_stream) {
      trailers = HeaderMapOptRef(std::ref(decoder_callbacks_->addDecodedTrailers()));
    }
    decompress(config_->requestDirectionConfig(), request_decompressor_, *decoder_callbacks_, data,
               request_byte_tracker_, trailers);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DecompressorFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  // Only report if the filter has actually decompressed.
  if (request_decompressor_) {
    request_byte_tracker_.reportTotalBytes(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
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

Http::FilterDataStatus DecompressorFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_decompressor_) {
    HeaderMapOptRef trailers;
    if (end_stream) {
      trailers = HeaderMapOptRef(std::ref(encoder_callbacks_->addEncodedTrailers()));
    }
    decompress(config_->responseDirectionConfig(), response_decompressor_, *encoder_callbacks_,
               data, response_byte_tracker_, trailers);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DecompressorFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  // Only report if the filter has actually decompressed.
  if (response_decompressor_) {
    response_byte_tracker_.reportTotalBytes(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

void DecompressorFilter::decompress(
    const DecompressorFilterConfig::DirectionConfig& direction_config,
    const Compression::Decompressor::DecompressorPtr& decompressor,
    Http::StreamFilterCallbacks& callbacks, Buffer::Instance& input_buffer,
    ByteTracker& byte_tracker, HeaderMapOptRef trailers) const {
  ASSERT(decompressor);
  Buffer::OwnedImpl output_buffer;
  decompressor->decompress(input_buffer, output_buffer);

  // Report decompression via stats and logging before modifying the input buffer.
  byte_tracker.chargeBytes(input_buffer.length(), output_buffer.length());
  direction_config.stats().total_compressed_bytes_.add(input_buffer.length());
  direction_config.stats().total_uncompressed_bytes_.add(output_buffer.length());
  ENVOY_STREAM_LOG(debug, "{} data decompressed from {} bytes to {} bytes", callbacks,
                   direction_config.logString(), input_buffer.length(), output_buffer.length());

  input_buffer.drain(input_buffer.length());
  input_buffer.add(output_buffer);

  if (trailers.has_value()) {
    byte_tracker.reportTotalBytes(trailers.value().get());
  }
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
DecompressorFilter::getCacheControlHandle() {
  return cache_control_request_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
DecompressorFilter::getCacheControlHandle() {
  return cache_control_response_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
DecompressorFilter::getContentEncodingHandle() {
  return content_encoding_request_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
DecompressorFilter::getContentEncodingHandle() {
  return content_encoding_response_handle.handle();
}

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
