#include "extensions/common/tap/tap_config_base.h"

#include <fstream>

#include "common/common/assert.h"
#include "common/common/stack_array.h"
#include "common/protobuf/utility.h"

#include "extensions/common/tap/tap_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

void Utility::addBufferToProtoBytes(envoy::data::tap::v2alpha::Body& output_bytes,
                                    uint32_t max_buffered_bytes, const Buffer::Instance& data) {
  // TODO(mattklein123): Figure out if we can use the buffer API here directly in some way. This is
  // is not trivial if we want to avoid extra copies since we end up appending to the existing
  // protobuf string.
  const uint64_t num_slices = data.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  data.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    if (slice.len_ > max_buffered_bytes - output_bytes.as_bytes().size()) {
      output_bytes.set_truncated(true);
    }

    output_bytes.mutable_as_bytes()->append(
        static_cast<const char*>(slice.mem_),
        std::min(slice.len_, max_buffered_bytes - output_bytes.as_bytes().size()));
  }
}

TapConfigBaseImpl::TapConfigBaseImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : max_buffered_rx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_rx_bytes, DefaultMaxBufferedBytes)),
      max_buffered_tx_bytes_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          proto_config.output_config(), max_buffered_tx_bytes, DefaultMaxBufferedBytes)) {
  ASSERT(proto_config.output_config().sinks().size() == 1);
  sink_format_ = proto_config.output_config().sinks()[0].format();
  switch (proto_config.output_config().sinks()[0].output_sink_type_case()) {
  case envoy::service::tap::v2alpha::OutputSink::kStreamingAdmin:
    // TODO(mattklein123): Graceful failure, error message, and test if someone specifies an
    // admin stream output without configuring via /tap or the wrong format.
    RELEASE_ASSERT(admin_streamer != nullptr, "admin output must be configured via admin");
    RELEASE_ASSERT(sink_format_ == envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_BYTES ||
                       sink_format_ ==
                           envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING,
                   "admin output only supports JSON formats");
    sink_to_use_ = admin_streamer;
    break;
  case envoy::service::tap::v2alpha::OutputSink::kFilePerTap:
    sink_ =
        std::make_unique<FilePerTapSink>(proto_config.output_config().sinks()[0].file_per_tap());
    sink_to_use_ = sink_.get();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  buildMatcher(proto_config.match_config(), matchers_);
}

Matcher& TapConfigBaseImpl::rootMatcher() const {
  ASSERT(matchers_.size() >= 1);
  return *matchers_[0];
}

namespace {
void swapBytesToString(envoy::data::tap::v2alpha::Body& body) {
  body.set_allocated_as_string(body.release_as_bytes());
}
} // namespace

void TapConfigBaseImpl::submitBufferedTrace(
    const std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper>& trace,
    uint64_t trace_id) {
  // Swap the "bytes" string into the "string" string. This is done purely so that JSON
  // serialization will serialize as a string vs. doing base64 encoding.
  if (sink_format_ == envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING) {
    switch (trace->trace_case()) {
    case envoy::data::tap::v2alpha::BufferedTraceWrapper::kHttpBufferedTrace: {
      auto* http_trace = trace->mutable_http_buffered_trace();
      if (http_trace->has_request() && http_trace->request().has_body()) {
        swapBytesToString(*http_trace->mutable_request()->mutable_body());
      }
      if (http_trace->has_response() && http_trace->response().has_body()) {
        swapBytesToString(*http_trace->mutable_response()->mutable_body());
      }
      break;
    }
    case envoy::data::tap::v2alpha::BufferedTraceWrapper::kSocketBufferedTrace: {
      auto* socket_trace = trace->mutable_socket_buffered_trace();
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
    case envoy::data::tap::v2alpha::BufferedTraceWrapper::TRACE_NOT_SET:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  sink_to_use_->submitBufferedTrace(trace, sink_format_, trace_id);
}

void FilePerTapSink::submitBufferedTrace(
    const std::shared_ptr<envoy::data::tap::v2alpha::BufferedTraceWrapper>& trace,
    envoy::service::tap::v2alpha::OutputSink::Format format, uint64_t trace_id) {
  std::string path = fmt::format("{}_{}", config_.path_prefix(), trace_id);
  switch (format) {
  case envoy::service::tap::v2alpha::OutputSink::PROTO_BINARY:
    path += MessageUtil::FileExtensions::get().ProtoBinary;
    break;
  case envoy::service::tap::v2alpha::OutputSink::PROTO_TEXT:
    path += MessageUtil::FileExtensions::get().ProtoText;
    break;
  case envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_BYTES:
  case envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING:
    path += MessageUtil::FileExtensions::get().Json;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  ENVOY_LOG_MISC(debug, "Writing tap for [id={}] to {}", trace_id, path);
  ENVOY_LOG_MISC(trace, "Tap for [id={}]: {}", trace_id, trace->DebugString());
  std::ofstream output_file(path);

  switch (format) {
  case envoy::service::tap::v2alpha::OutputSink::PROTO_BINARY:
    trace->SerializeToOstream(&output_file);
    break;
  case envoy::service::tap::v2alpha::OutputSink::PROTO_TEXT:
    output_file << trace->DebugString();
    break;
  case envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_BYTES:
  case envoy::service::tap::v2alpha::OutputSink::JSON_BODY_AS_STRING:
    output_file << MessageUtil::getJsonStringFromMessage(*trace, true, true);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
