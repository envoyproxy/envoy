#include "source/extensions/common/tap/tap_config_base.h"

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/matcher/matcher.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

using namespace Matcher;

bool Utility::addBufferToProtoBytes(envoy::data::tap::v3::Body& output_body,
                                    uint32_t max_buffered_bytes, const Buffer::Instance& data,
                                    uint32_t buffer_start_offset, uint32_t buffer_length_to_copy) {
  // TODO(mattklein123): Figure out if we can use the buffer API here directly in some way. This is
  // is not trivial if we want to avoid extra copies since we end up appending to the existing
  // protobuf string.

  // Note that max_buffered_bytes is assumed to include any data already contained in output_bytes.
  // This is to account for callers that may be tracking this over multiple body objects.
  ASSERT(buffer_start_offset + buffer_length_to_copy <= data.length());
  const uint32_t final_bytes_to_copy = std::min(max_buffered_bytes, buffer_length_to_copy);

  Buffer::RawSliceVector slices = data.getRawSlices();
  trimSlices(slices, buffer_start_offset, final_bytes_to_copy);
  for (const Buffer::RawSlice& slice : slices) {
    output_body.mutable_as_bytes()->append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  if (final_bytes_to_copy < buffer_length_to_copy) {
    output_body.set_truncated(true);
    return true;
  } else {
    return false;
  }
}

TapConfigBaseImpl::TapConfigBaseImpl(const envoy::config::tap::v3::TapConfig& proto_config,
                                     Common::Tap::Sink* admin_streamer, SinkContext context)
    : max_buffered_rx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_rx_bytes, DefaultMaxBufferedBytes)),
      max_buffered_tx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_tx_bytes, DefaultMaxBufferedBytes)),
      streaming_(proto_config.output_config().streaming()) {

  using TsfContextRef =
      std::reference_wrapper<Server::Configuration::TransportSocketFactoryContext>;
  using HttpContextRef = std::reference_wrapper<Server::Configuration::FactoryContext>;
  using ProtoOutputSink = envoy::config::tap::v3::OutputSink;
  auto& sinks = proto_config.output_config().sinks();
  ASSERT(sinks.size() == 1);
  // TODO(mattklein123): Add per-sink checks to make sure format makes sense. I.e., when using
  // streaming, we should require the length delimited version of binary proto, etc.
  sink_format_ = sinks[0].format();
  sink_type_ = sinks[0].output_sink_type_case();

  switch (sink_type_) {
  case ProtoOutputSink::OutputSinkTypeCase::kBufferedAdmin:
    if (admin_streamer == nullptr) {
      throw EnvoyException(fmt::format("Output sink type BufferedAdmin requires that the admin "
                                       "output will be configured via admin"));
    }
    // TODO(mattklein123): Graceful failure, error message, and test if someone specifies an
    // admin stream output with the wrong format.
    RELEASE_ASSERT(
        sink_format_ == ProtoOutputSink::JSON_BODY_AS_BYTES ||
            sink_format_ == ProtoOutputSink::JSON_BODY_AS_STRING ||
            sink_format_ == ProtoOutputSink::PROTO_BINARY_LENGTH_DELIMITED,
        "buffered admin output only supports JSON or length delimited proto binary formats");
    sink_to_use_ = admin_streamer;
    break;
  case ProtoOutputSink::OutputSinkTypeCase::kStreamingAdmin:
    if (admin_streamer == nullptr) {
      throw EnvoyException(fmt::format("Output sink type StreamingAdmin requires that the admin "
                                       "output will be configured via admin"));
    }
    // TODO(mattklein123): Graceful failure, error message, and test if someone specifies an
    // admin stream output with the wrong format.
    // TODO(davidpeet8): Simple change to enable PROTO_BINARY_LENGTH_DELIMITED format -
    // functionality already implemented for kBufferedAdmin
    RELEASE_ASSERT(sink_format_ == ProtoOutputSink::JSON_BODY_AS_BYTES ||
                       sink_format_ == ProtoOutputSink::JSON_BODY_AS_STRING,
                   "streaming admin output only supports JSON formats");
    sink_to_use_ = admin_streamer;
    break;
  case ProtoOutputSink::OutputSinkTypeCase::kFilePerTap:
    sink_ = std::make_unique<FilePerTapSink>(sinks[0].file_per_tap());
    sink_to_use_ = sink_.get();
    break;
  case ProtoOutputSink::OutputSinkTypeCase::kCustomSink: {
    TapSinkFactory& tap_sink_factory =
        Envoy::Config::Utility::getAndCheckFactory<TapSinkFactory>(sinks[0].custom_sink());

    // extract message validation visitor from the context and use it to define config
    ProtobufTypes::MessagePtr config;
    if (absl::holds_alternative<TsfContextRef>(context)) {
      Server::Configuration::TransportSocketFactoryContext& tsf_context =
          absl::get<TsfContextRef>(context).get();
      config = Config::Utility::translateAnyToFactoryConfig(sinks[0].custom_sink().typed_config(),
                                                            tsf_context.messageValidationVisitor(),
                                                            tap_sink_factory);
    } else {
      Server::Configuration::FactoryContext& http_context =
          absl::get<HttpContextRef>(context).get();
      config = Config::Utility::translateAnyToFactoryConfig(
          sinks[0].custom_sink().typed_config(),
          http_context.serverFactoryContext().messageValidationContext().staticValidationVisitor(),
          tap_sink_factory);
    }

    sink_ = tap_sink_factory.createSinkPtr(*config, context);
    sink_to_use_ = sink_.get();
    break;
  }
  case envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kStreamingGrpc:
    PANIC("not implemented");
  case envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::OUTPUT_SINK_TYPE_NOT_SET:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  envoy::config::common::matcher::v3::MatchPredicate match;
  if (proto_config.has_match()) {
    // Use the match field whenever it is set.
    match = proto_config.match();
  } else if (proto_config.has_match_config()) {
    // Fallback to use the deprecated match_config field and upgrade (wire cast) it to the new
    // MatchPredicate which is backward compatible with the old MatchPredicate originally
    // introduced in the Tap filter.
    MessageUtil::wireCast(proto_config.match_config(), match);
  } else {
    throw EnvoyException(fmt::format("Neither match nor match_config is set in TapConfig: {}",
                                     proto_config.DebugString()));
  }

  Server::Configuration::CommonFactoryContext* server_context = nullptr;
  if (absl::holds_alternative<TsfContextRef>(context)) {
    Server::Configuration::TransportSocketFactoryContext& tsf_context =
        absl::get<TsfContextRef>(context).get();
    server_context = &tsf_context.serverFactoryContext();
  } else {
    Server::Configuration::FactoryContext& http_context = absl::get<HttpContextRef>(context).get();
    server_context = &http_context.serverFactoryContext();
  }
  buildMatcher(match, matchers_, *server_context);
}

const Matcher& TapConfigBaseImpl::rootMatcher() const {
  ASSERT(!matchers_.empty());
  return *matchers_[0];
}

namespace {
void swapBytesToString(envoy::data::tap::v3::Body& body) {
  body.set_allocated_as_string(body.release_as_bytes());
}
} // namespace

void Utility::bodyBytesToString(envoy::data::tap::v3::TraceWrapper& trace,
                                envoy::config::tap::v3::OutputSink::Format sink_format) {
  // Swap the "bytes" string into the "string" string. This is done purely so that JSON
  // serialization will serialize as a string vs. doing base64 encoding.
  if (sink_format != envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
    return;
  }

  switch (trace.trace_case()) {
  case envoy::data::tap::v3::TraceWrapper::TraceCase::kHttpBufferedTrace: {
    auto* http_trace = trace.mutable_http_buffered_trace();
    if (http_trace->has_request() && http_trace->request().has_body()) {
      swapBytesToString(*http_trace->mutable_request()->mutable_body());
    }
    if (http_trace->has_response() && http_trace->response().has_body()) {
      swapBytesToString(*http_trace->mutable_response()->mutable_body());
    }
    break;
  }
  case envoy::data::tap::v3::TraceWrapper::TraceCase::kHttpStreamedTraceSegment: {
    auto* http_trace = trace.mutable_http_streamed_trace_segment();
    if (http_trace->has_request_body_chunk()) {
      swapBytesToString(*http_trace->mutable_request_body_chunk());
    }
    if (http_trace->has_response_body_chunk()) {
      swapBytesToString(*http_trace->mutable_response_body_chunk());
    }
    break;
  }
  case envoy::data::tap::v3::TraceWrapper::TraceCase::kSocketBufferedTrace: {
    auto* socket_trace = trace.mutable_socket_buffered_trace();
    for (auto& event : *socket_trace->mutable_events()) {
      if (event.has_read()) {
        swapBytesToString(*event.mutable_read()->mutable_data());
      } else {
        ASSERT(event.has_write());
        swapBytesToString(*event.mutable_write()->mutable_data());
      }
    }
    break;
  }
  case envoy::data::tap::v3::TraceWrapper::TraceCase::kSocketStreamedTraceSegment: {
    auto& event = *trace.mutable_socket_streamed_trace_segment()->mutable_event();
    if (event.has_read()) {
      swapBytesToString(*event.mutable_read()->mutable_data());
    } else if (event.has_write()) {
      swapBytesToString(*event.mutable_write()->mutable_data());
    }
    break;
  }
  case envoy::data::tap::v3::TraceWrapper::TraceCase::TRACE_NOT_SET:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
}

void TapConfigBaseImpl::PerTapSinkHandleManagerImpl::submitTrace(TraceWrapperPtr&& trace) {
  Utility::bodyBytesToString(*trace, parent_.sink_format_);
  handle_->submitTrace(std::move(trace), parent_.sink_format_);
}

void FilePerTapSink::FilePerTapSinkHandle::submitTrace(
    TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format format) {
  if (!output_file_.is_open()) {
    std::string path = fmt::format("{}_{}", parent_.config_.path_prefix(), trace_id_);
    switch (format) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::tap::v3::OutputSink::PROTO_BINARY:
      path += MessageUtil::FileExtensions::get().ProtoBinary;
      break;
    case envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED:
      path += MessageUtil::FileExtensions::get().ProtoBinaryLengthDelimited;
      break;
    case envoy::config::tap::v3::OutputSink::PROTO_TEXT:
      path += MessageUtil::FileExtensions::get().ProtoText;
      break;
    case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
    case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING:
      path += MessageUtil::FileExtensions::get().Json;
      break;
    }

    ENVOY_LOG_MISC(debug, "Opening tap file for [id={}] to {}", trace_id_, path);
    // When reading and writing binary files, we need to be sure std::ios_base::binary
    // is set, otherwise we will not get the expected results on Windows
    output_file_.open(path, std::ios_base::binary);
  }

  ENVOY_LOG_MISC(trace, "Tap for [id={}]: {}", trace_id_, trace->DebugString());

  switch (format) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY:
    trace->SerializeToOstream(&output_file_);
    break;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED: {
    Protobuf::io::OstreamOutputStream stream(&output_file_);
    Protobuf::io::CodedOutputStream coded_stream(&stream);
    coded_stream.WriteVarint32(trace->ByteSize());
    trace->SerializeWithCachedSizes(&coded_stream);
    break;
  }
  case envoy::config::tap::v3::OutputSink::PROTO_TEXT:
    output_file_ << MessageUtil::toTextProto(*trace);
    break;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING:
    output_file_ << MessageUtil::getJsonStringFromMessageOrError(*trace, true, true);
    break;
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
