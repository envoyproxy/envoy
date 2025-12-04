#include "source/extensions/filters/http/grpc_decompressor/decompressor_filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcDecompressor {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_grpc_accept_encoding_handle(Http::CustomHeaders::get().GrpcAcceptEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_grpc_accept_encoding_handle(Http::CustomHeaders::get().GrpcAcceptEncoding);

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    request_grpc_encoding_handle(Http::CustomHeaders::get().GrpcEncoding);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    response_grpc_encoding_handle(Http::CustomHeaders::get().GrpcEncoding);

void decompressAndUpdateStats(Compression::Decompressor::DecompressorFactory& decompressor_factory,
                              const CommonDecompressorStats& stats, const std::string& stat_prefix,
                              Grpc::Frame& frame) {
  if ((frame.flags_ & Grpc::GRPC_FH_COMPRESSED) == 0) {
    return;
  }

  auto decompressor = decompressor_factory.createDecompressor(
      stat_prefix); // each message is decompressed individually

  Buffer::OwnedImpl output_buffer;
  decompressor->decompress(*frame.data_, output_buffer);

  stats.total_compressed_bytes_.add(frame.data_->length());
  stats.total_uncompressed_bytes_.add(output_buffer.length());

  frame.flags_ = frame.flags_ & ~Grpc::GRPC_FH_COMPRESSED;
  frame.length_ = output_buffer.length();
  frame.data_->drain(frame.data_->length());
  frame.data_->add(output_buffer);
}

void flushAndResetFrames(Buffer::Instance& output, std::vector<Grpc::Frame>& frames,
                         Compression::Decompressor::DecompressorFactory& decompressor_factory,
                         Grpc::Encoder& encoder, const CommonDecompressorStats& stats,
                         const std::string& stat_prefix) {
  for (auto& frame : frames) {
    decompressAndUpdateStats(decompressor_factory, stats, stat_prefix, frame);
    encoder.prependFrameHeader(frame.flags_, *frame.data_);
    output.move(*frame.data_);
  }
  frames.clear();
}

} // namespace

DecompressorFilterConfig::DirectionConfig::DirectionConfig(
    const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor::
        CommonDirectionConfig& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : stats_(generateStats(stats_prefix, scope)),
      decompression_enabled_(proto_config.enabled(), runtime),
      advertise_grpc_accept_encoding_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, advertise_grpc_accept_encoding, true)),
      max_message_length_{PROTOBUF_GET_OPTIONAL_WRAPPED(proto_config, max_message_length)} {}

DecompressorFilterConfig::RequestDirectionConfig::RequestDirectionConfig(
    const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor::
        RequestDirectionConfig& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.common_config(), stats_prefix + "request.", scope, runtime) {}

DecompressorFilterConfig::ResponseDirectionConfig::ResponseDirectionConfig(
    const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor::
        ResponseDirectionConfig& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime)
    : DirectionConfig(proto_config.common_config(), stats_prefix + "response.", scope, runtime) {}

DecompressorFilterConfig::DecompressorFilterConfig(
    const envoy::extensions::filters::http::grpc_decompressor::v3::Decompressor& proto_config,
    const std::string& stats_prefix, Stats::Scope& scope, Runtime::Loader& runtime,
    Compression::Decompressor::DecompressorFactoryPtr decompressor_factory)
    : stats_prefix_(fmt::format("{}decompressor.{}.{}", stats_prefix,
                                proto_config.decompressor_library().name(),
                                decompressor_factory->statsPrefix())),
      decompressor_stats_prefix_(stats_prefix_ + "decompressor_library"),
      decompressor_factory_(std::move(decompressor_factory)),
      request_direction_config_(proto_config.request_direction_config(), stats_prefix_, scope,
                                runtime),
      response_direction_config_(proto_config.response_direction_config(), stats_prefix_, scope,
                                 runtime) {}

DecompressorFilter::DecompressorFilter(DecompressorFilterConfigSharedPtr config)
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

Http::FilterHeadersStatus DecompressorFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  is_grpc_ = Grpc::Common::isGrpcRequestHeaders(headers);
  if (!is_grpc_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Two responsibilities on the request side:
  //   1. If response decompression is enabled (and advertisement is enabled), then advertise to
  //      the upstream that this hop is able to decompress responses via the grpc-accept-encoding
  //      header.
  if (config_->responseDirectionConfig().decompressionEnabled() &&
      config_->requestDirectionConfig().advertiseGrpcAcceptEncoding()) {
    // grpc-accept-encoding behaves like accept-encoding, except without q-values, so we can use the
    // same utility function to add the encoding.
    const std::string new_grpc_accept_encoding_header =
        Http::HeaderUtility::addEncodingToAcceptEncoding(
            headers.getInlineValue(request_grpc_accept_encoding_handle.handle()),
            getGrpcEncoding());
    headers.setInline(request_grpc_accept_encoding_handle.handle(),
                      new_grpc_accept_encoding_header);
  }

  request_decompression_enabled_ = shouldDecompress(config_->requestDirectionConfig(), headers);

  return Http::FilterHeadersStatus::Continue;
};

Http::FilterDataStatus DecompressorFilter::decodeData(Buffer::Instance& data, bool) {
  if (!request_decompression_enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  const auto& request_config = config_->requestDirectionConfig();
  auto status = request_decoder_.decode(data, request_frames_);
  if (!status.ok()) {
    // Sending a response is not guaranteed to be sent to the client, since the response may already
    // be in flight. This will in that case simply reset the stream.
    if (status.code() == absl::StatusCode::kResourceExhausted) {
      request_config.stats().message_length_too_large_.inc();
      decoder_callbacks_->sendLocalReply(Http::Code::PayloadTooLarge,
                                         "Request message length too large", nullptr,
                                         Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                                         "grpc_decompressor_rq_msg_too_large");
    } else {
      request_config.stats().message_decoding_error_.inc();
      decoder_callbacks_->sendLocalReply(
          Http::Code::InternalServerError, "Could not decode message in request direction", nullptr,
          Grpc::Status::WellKnownGrpcStatus::Internal, "grpc_decompressor_rq_msg_decode_error");
    }

    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  flushAndResetFrames(data, request_frames_, config_->decompressorFactory(), request_encoder_,
                      request_config.stats(), config_->decompressorStatsPrefix());
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DecompressorFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterHeadersStatus DecompressorFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                            bool) {
  if (!is_grpc_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Two responsibilities on the response side:
  //   1. If request decompression is enabled (and advertisement is enabled), then advertise to
  //      the downstream that this hop is able to decompress requests via the grpc-accept-encoding
  //      header.
  if (config_->requestDirectionConfig().decompressionEnabled() &&
      config_->responseDirectionConfig().advertiseGrpcAcceptEncoding()) {
    const std::string new_grpc_accept_encoding_header =
        Http::HeaderUtility::addEncodingToAcceptEncoding(
            headers.getInlineValue(response_grpc_accept_encoding_handle.handle()),
            getGrpcEncoding());
    headers.setInline(response_grpc_accept_encoding_handle.handle(),
                      new_grpc_accept_encoding_header);
  }

  response_decompression_enabled_ = shouldDecompress(config_->responseDirectionConfig(), headers);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus DecompressorFilter::encodeData(Buffer::Instance& data, bool) {
  if (!response_decompression_enabled_) {
    return Http::FilterDataStatus::Continue;
  }

  const auto& response_config = config_->responseDirectionConfig();
  auto status = response_decoder_.decode(data, response_frames_);
  if (!status.ok()) {
    // Sending a response is not guaranteed to be sent to the client, since the response may already
    // be in flight. This will in that case simply reset the stream.
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

  flushAndResetFrames(data, response_frames_, config_->decompressorFactory(), response_encoder_,
                      response_config.stats(), config_->decompressorStatsPrefix());
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus DecompressorFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
DecompressorFilter::getGrpcEncodingHandle() {
  return request_grpc_encoding_handle.handle();
}

template <>
Http::CustomInlineHeaderRegistry::Handle<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
DecompressorFilter::getGrpcEncodingHandle() {
  return response_grpc_encoding_handle.handle();
}

} // namespace GrpcDecompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
