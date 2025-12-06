#include "source/extensions/filters/http/grpc_compressor/compressor_filter.h"

#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcCompressor {

namespace {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_grpc_accept_encoding_handle(Http::CustomHeaders::get().GrpcAcceptEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_grpc_accept_encoding_handle(Http::CustomHeaders::get().GrpcAcceptEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_grpc_encoding_handle(Http::CustomHeaders::get().GrpcEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_grpc_encoding_handle(Http::CustomHeaders::get().GrpcEncoding);

// Default minimum length of an upstream response that allows compression.
const uint32_t DefaultMinimumMessageLength = 30;

void compressAndUpdateStats(Compression::Compressor::CompressorFactory& compressor_factory,
                            const CommonCompressorStats& stats, Grpc::Frame& frame) {
  auto compressor =
      compressor_factory.createCompressor(); // each message is compressed individually
  stats.total_uncompressed_bytes_.add(frame.data_->length());
  compressor->compress(*frame.data_, Envoy::Compression::Compressor::State::Finish);
  stats.total_compressed_bytes_.add(frame.data_->length());
  frame.flags_ = frame.flags_ | Grpc::GRPC_FH_COMPRESSED;
  frame.length_ = frame.data_->length();
}

void flushAndResetFrames(Buffer::Instance& output, std::vector<Grpc::Frame>& frames,
                         Compression::Compressor::CompressorFactory& compressor_factory,
                         Grpc::Encoder& encoder, const CommonCompressorStats& stats) {
  for (auto& frame : frames) {
    compressAndUpdateStats(compressor_factory, stats, frame);
    encoder.prependFrameHeader(frame.flags_, *frame.data_);
    output.move(*frame.data_);
  }
  frames.clear();
}

} // namespace

CompressorFilterConfig::DirectionConfig::DirectionConfig(
    const envoy::extensions::filters::http::grpc_compressor::v3::Compressor::CommonDirectionConfig&
        proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : compression_enabled_(proto_config.enabled(), runtime),
      remove_grpc_accept_encoding_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, remove_grpc_accept_encoding, false)),
      min_message_length_{PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, min_message_length,
                                                          DefaultMinimumMessageLength)},
      max_message_length_{PROTOBUF_GET_OPTIONAL_WRAPPED(proto_config, max_message_length)},
      stats_{generateStats(stats_prefix, scope)} {}

CompressorFilterConfig::RequestDirectionConfig::RequestDirectionConfig(
    const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.request_direction_config().common_config(),
                      stats_prefix + "request.", scope, runtime),
      is_set_{proto_config.has_request_direction_config()} {}

CompressorFilterConfig::ResponseDirectionConfig::ResponseDirectionConfig(
    const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.response_direction_config().common_config(),
                      stats_prefix + "response.", scope, runtime),
      is_set_{proto_config.has_response_direction_config()} {}

CompressorFilterConfig::CompressorFilterConfig(
    const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Compression::Compressor::CompressorFactoryPtr compressor_factory)
    : common_stats_prefix_(fmt::format("{}grpc_compressor.{}.{}", stats_prefix,
                                       proto_config.compressor_library().name(),
                                       compressor_factory->statsPrefix())),
      request_direction_config_(proto_config, common_stats_prefix_, scope, runtime),
      response_direction_config_(proto_config, common_stats_prefix_, scope, runtime),
      grpc_encoding_(
          compressor_factory
              ->contentEncoding()), // so far, grpc matches content encoding value for http
      compressor_factory_(std::move(compressor_factory)) {}

CompressorFilter::CompressorFilter(const CompressorFilterConfigSharedPtr config)
    : config_(std::move(config)) {
  if (config_->requestDirectionConfig().maximumMessageLength().has_value()) {
    request_decoder_.setMaxFrameLength(
        config_->requestDirectionConfig().maximumMessageLength().value());
  }
  if (config_->responseDirectionConfig().maximumMessageLength().has_value()) {
    response_decoder_.setMaxFrameLength(
        config_->responseDirectionConfig().maximumMessageLength().value());
  }
}

Http::FilterHeadersStatus CompressorFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  is_grpc_ = Grpc::Common::isGrpcRequestHeaders(headers);
  if (!is_grpc_) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto& request_config = config_->requestDirectionConfig();
  const auto& response_config = config_->responseDirectionConfig();

  // Handle response direction
  const Http::HeaderEntry* accept_encoding =
      headers.getInline(request_grpc_accept_encoding_handle.handle());
  if (accept_encoding != nullptr) {
    // Capture the value of the "Accept-Encoding" request header to use it later when making
    // decision on compressing the corresponding HTTP response.
    request_grpc_accept_encoding_ = accept_encoding->value().getStringView();
  }

  // Handle request direction
  // Check removing encoding from accept encoding header if response decompression is enabled.
  if (compressionEnabled(response_config) && request_config.removeGrpcAcceptEncoding()) {
    removeGrpcAcceptEncoding(headers);
  }

  request_compression_enabled_ = compressionEnabled(request_config) &&
                                 isGrpcEncodingAllowed(headers) && connectionAllowsGrpcEncoding();
  if (request_compression_enabled_) {
    headers.setInline(request_grpc_encoding_handle.handle(), getGrpcEncoding());
    request_config.stats().compressed_rpc_.inc();
  } else {
    request_config.stats().not_compressed_rpc_.inc();
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CompressorFilter::decodeData(Buffer::Instance& data, bool) {
  if (!request_compression_enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  const auto& request_config = config_->requestDirectionConfig();
  absl::Status status = request_decoder_.decode(data, request_frames_);
  if (!status.ok()) {
    // Sending a response is not guaranteed to be sent to the client, since the response may already
    // be in flight. This will in that case simply reset the stream.
    if (status.code() == absl::StatusCode::kResourceExhausted) {
      request_config.stats().message_length_too_large_.inc();
      decoder_callbacks_->sendLocalReply(
          Http::Code::PayloadTooLarge, "Request message length too large", nullptr,
          Grpc::Status::WellKnownGrpcStatus::ResourceExhausted, "grpc_compressor_rq_msg_too_large");
    } else {
      request_config.stats().message_decoding_error_.inc();
      decoder_callbacks_->sendLocalReply(
          Http::Code::InternalServerError, "Could not decode message in request direction", nullptr,
          Grpc::Status::WellKnownGrpcStatus::Internal, "grpc_compressor_rq_msg_decode_error");
    }

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  flushAndResetFrames(data, request_frames_, config_->compressorFactory(), request_encoder_,
                      request_config.stats());

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus CompressorFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus CompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (!is_grpc_) {
    return Http::FilterHeadersStatus::Continue;
  }

  const auto& request_config = config_->requestDirectionConfig();
  const auto& response_config = config_->responseDirectionConfig();

  // Check adding encoding to accept encoding header if request compression is enabled.
  if (compressionEnabled(request_config) && response_config.removeGrpcAcceptEncoding()) {
    removeGrpcAcceptEncoding(headers);
  }

  // Handle response direction
  response_compression_enabled_ =
      compressionEnabled(response_config) && isGrpcEncodingAllowed(headers) &&
      isGrpcAcceptEncodingAllowed(request_grpc_accept_encoding_.value_or(""));
  if (response_compression_enabled_) {
    headers.setInline(response_grpc_encoding_handle.handle(), getGrpcEncoding());
    response_config.stats().compressed_rpc_.inc();
  } else {
    response_config.stats().not_compressed_rpc_.inc();
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CompressorFilter::encodeData(Buffer::Instance& data, bool) {
  if (!response_compression_enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  const auto& response_config = config_->responseDirectionConfig();
  absl::Status status = response_decoder_.decode(data, response_frames_);
  if (!status.ok()) {
    // Since the response is already in flight, we can only reset the stream.
    if (status.code() == absl::StatusCode::kResourceExhausted) {
      response_config.stats().message_length_too_large_.inc();
      decoder_callbacks_->resetStream(Http::StreamResetReason::LocalReset,
                                      "Response message length too large");
    } else {
      response_config.stats().message_decoding_error_.inc();
      decoder_callbacks_->resetStream(Http::StreamResetReason::LocalReset,
                                      "Could not decode message in response direction");
    }

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  flushAndResetFrames(data, response_frames_, config_->compressorFactory(), response_encoder_,
                      response_config.stats());

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus CompressorFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

bool CompressorFilter::compressionEnabled(const DirectionConfigOptRef config) const {
  return config->compressionEnabled();
}

std::string CompressorFilter::getGrpcEncoding() const { return config_->grpcEncoding(); }

bool CompressorFilter::connectionAllowsGrpcEncoding() const {
  // TODO(wtzhang23): Handle request direction caching grpc-accept-encoding header for subsequent
  // requests made on the same connection.
  return true;
}

bool CompressorFilter::isGrpcAcceptEncodingAllowed(
    const absl::string_view grpc_accept_encoding) const {
  for (absl::string_view header_value : StringUtil::splitToken(grpc_accept_encoding, ",")) {
    const auto trimmed_value = StringUtil::trim(header_value);
    if (absl::EqualsIgnoreCase(trimmed_value, getGrpcEncoding())) {
      return true;
    }
  }
  return false;
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
CompressorFilter::getGrpcEncodingHandle() {
  return request_grpc_encoding_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
CompressorFilter::getGrpcEncodingHandle() {
  return response_grpc_encoding_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
CompressorFilter::getGrpcAcceptEncodingHandle() {
  return request_grpc_accept_encoding_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
CompressorFilter::getGrpcAcceptEncodingHandle() {
  return response_grpc_accept_encoding_handle.handle();
}

} // namespace GrpcCompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
