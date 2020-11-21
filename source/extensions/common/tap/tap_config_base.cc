#include "extensions/common/tap/tap_config_base.h"

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/version_converter.h"
#include "common/protobuf/utility.h"

#include "extensions/common/matcher/matcher.h"

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
                                     Common::Tap::Sink* admin_streamer)
    : max_buffered_rx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_rx_bytes, DefaultMaxBufferedBytes)),
      max_buffered_tx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_tx_bytes, DefaultMaxBufferedBytes)),
      streaming_(proto_config.output_config().streaming()) {
  ASSERT(proto_config.output_config().sinks().size() == 1);
  // TODO(mattklein123): Add per-sink checks to make sure format makes sense. I.e., when using
  // streaming, we should require the length delimited version of binary proto, etc.
  sink_format_ = proto_config.output_config().sinks()[0].format();
  switch (proto_config.output_config().sinks()[0].output_sink_type_case()) {
  case envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kStreamingAdmin:
    ASSERT(admin_streamer != nullptr, "admin output must be configured via admin");
    // TODO(mattklein123): Graceful failure, error message, and test if someone specifies an
    // admin stream output with the wrong format.
    RELEASE_ASSERT(sink_format_ == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES ||
                       sink_format_ == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING,
                   "admin output only supports JSON formats");
    sink_to_use_ = admin_streamer;
    break;
  case envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kFilePerTap:
    sink_ =
        std::make_unique<FilePerTapSink>(proto_config.output_config().sinks()[0].file_per_tap());
    sink_to_use_ = sink_.get();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  envoy::config::common::matcher::v3::MatchPredicate match;
  if (proto_config.has_match()) {
    // Use the match field whenever it is set.
    match = proto_config.match();
  } else if (proto_config.has_match_config()) {
    // Fallback to use the deprecated match_config field and upgrade (wire cast) it to the new
    // MatchPredicate which is backward compatible with the old MatchPredicate originally
    // introduced in the Tap filter.
    Config::VersionConverter::upgrade(proto_config.match_config(), match);
  } else {
    throw EnvoyException(fmt::format("Neither match nor match_config is set in TapConfig: {}",
                                     proto_config.DebugString()));
  }
  buildMatcher(match, matchers_);
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
    NOT_REACHED_GCOVR_EXCL_LINE;
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
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    ENVOY_LOG_MISC(debug, "Opening tap file for [id={}] to {}", trace_id_, path);
    // When reading and writing binary files, we need to be sure std::ios_base::binary
    // is set, otherwise we will not get the expected results on Windows
    output_file_.open(path, std::ios_base::binary);
  }

  ENVOY_LOG_MISC(trace, "Tap for [id={}]: {}", trace_id_, trace->DebugString());

  switch (format) {
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
    output_file_ << trace->DebugString();
    break;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING:
    output_file_ << MessageUtil::getJsonStringFromMessage(*trace, true, true);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
