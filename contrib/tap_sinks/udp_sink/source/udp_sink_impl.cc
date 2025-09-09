#include "contrib/tap_sinks/udp_sink/source/udp_sink_impl.h"

#include "source/common/common/assert.h"
#include "source/common/network/utility.h"

#include "contrib/envoy/extensions/tap_sinks/udp_sink/v3alpha/udp_sink.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

UdpTapSink::UdpTapSink(const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink& config)
    : config_(config) {
  if (config_.udp_address().protocol() != envoy::config::core::v3::SocketAddress::UDP) {
    ENVOY_LOG_MISC(warn, "{}: Only suport UDP and invalid protocol", __func__);
    return;
  }

  // Verify the address (ipv4/ipv6).
  udp_server_address_ = Network::Utility::parseInternetAddressNoThrow(
      config_.udp_address().address(), static_cast<uint16_t>(config_.udp_address().port_value()),
      false);
  if (!udp_server_address_) {
    ENVOY_LOG_MISC(warn, "{}: Invalid configuration for address {} or port_value {}", __func__,
                   config_.udp_address().address().c_str(), config_.udp_address().port_value());
    return;
  }

  // Create socket.
  udp_socket_ =
      std::make_unique<Network::SocketImpl>(Network::Socket::Type::Datagram, udp_server_address_,
                                            nullptr, Network::SocketCreationOptions{});

  // Create udp writer.
  udp_packet_writer_ = std::make_unique<Network::UdpDefaultWriter>(udp_socket_->ioHandle());
  ENVOY_LOG_MISC(trace, "{}: UDP packet writer is created", __func__);
}

UdpTapSink::~UdpTapSink() { ENVOY_LOG_MISC(trace, "{}: UDP UdpTapSink() is called", __func__); }

uint32_t
UdpTapSink::getUdpMaxSendMsgDataSize(envoy::config::tap::v3::OutputSink::Format format) const {
  if (format == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
    return udp_max_send_msg_size_string_;
  } else {
    return udp_max_send_msg_size_bytes_;
  }
}

// UDP Tap sink hanlde
void UdpTapSink::UdpTapSinkHandle::setStreamedTraceDataAndSubmit(
    int32_t new_trace_cnt,
    const envoy::data::tap::v3::SocketStreamedTraceSegment& src_streamed_trace, bool is_read_event,
    size_t copy_offset, size_t copy_total_bytes,
    envoy::config::tap::v3::OutputSink::Format format) {

  TapCommon::TraceWrapperPtr dst_trace = std::make_unique<envoy::data::tap::v3::TraceWrapper>();
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *dst_trace->mutable_socket_streamed_trace_segment();

  // Set data from original trace to new trace.
  dst_streamed_trace.set_trace_id(src_streamed_trace.trace_id());

  Protobuf::Timestamp* dst_ts = dst_streamed_trace.mutable_event()->mutable_timestamp();
  dst_ts->CopyFrom(src_streamed_trace.event().timestamp());
  dst_ts->set_nanos(dst_ts->nanos() + new_trace_cnt);

  dst_streamed_trace.mutable_event()->mutable_connection()->CopyFrom(
      src_streamed_trace.event().connection());

  if (is_read_event) {
    if (format == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
      dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_string(
          src_streamed_trace.event().read().data().as_string().data() + copy_offset,
          copy_total_bytes);
    } else {
      dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_bytes(
          src_streamed_trace.event().read().data().as_bytes().data() + copy_offset,
          copy_total_bytes);
    }
  } else {
    if (format == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
      dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_string(
          src_streamed_trace.event().write().data().as_string().data() + copy_offset,
          copy_total_bytes);
    } else {
      dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_bytes(
          src_streamed_trace.event().write().data().as_bytes().data() + copy_offset,
          copy_total_bytes);
    }
  }

  doSubmitTrace(std::move(dst_trace), format);
}

void UdpTapSink::UdpTapSinkHandle::handleSocketStreamedTrace(
    TapCommon::TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format format) {

  const envoy::data::tap::v3::SocketStreamedTraceSegment& src_streamed_trace =
      trace->socket_streamed_trace_segment();

  if (src_streamed_trace.has_events()) {
    // Handle events in next PR.
    doSubmitTrace(std::move(trace), format);
    return;
  }

  // Handle single event
  size_t total_body_bytes = 0;
  bool is_read_event = false;
  if (src_streamed_trace.event().has_read()) {
    is_read_event = true;
    if (format == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
      total_body_bytes = src_streamed_trace.event().read().data().as_string().size();
    } else {
      total_body_bytes = src_streamed_trace.event().read().data().as_bytes().size();
    }
  } else {
    if (format == envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING) {
      total_body_bytes = src_streamed_trace.event().write().data().as_string().size();
    } else {
      total_body_bytes = src_streamed_trace.event().write().data().as_bytes().size();
    }
  }

  size_t max_size_of_each_sub_data = static_cast<size_t>(parent_.getUdpMaxSendMsgDataSize(format));
  if (total_body_bytes <= max_size_of_each_sub_data) {
    // Submit directly as normal.
    doSubmitTrace(std::move(trace), format);
    return;
  }

  // Slice data part and send each slice.
  size_t remaining_data_size = 0;
  size_t copy_offset = 0;
  int32_t new_trace_cnt = 0;
  while (true) {
    new_trace_cnt++;

    setStreamedTraceDataAndSubmit(new_trace_cnt, src_streamed_trace, is_read_event, copy_offset,
                                  max_size_of_each_sub_data, format);

    remaining_data_size = total_body_bytes - new_trace_cnt * max_size_of_each_sub_data;
    copy_offset = new_trace_cnt * max_size_of_each_sub_data;
    if (remaining_data_size == 0) {
      // No data left.
      break;
    }

    if (remaining_data_size < max_size_of_each_sub_data) {
      // The last part data, set and send.
      new_trace_cnt++;
      setStreamedTraceDataAndSubmit(new_trace_cnt, src_streamed_trace, is_read_event, copy_offset,
                                    remaining_data_size, format);
      break;
    }
  }
}

void UdpTapSink::UdpTapSinkHandle::doSubmitTrace(
    TapCommon::TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format format) {
  std::string json_string;
  if (format == envoy::config::tap::v3::OutputSink::PROTO_TEXT) {
    json_string = MessageUtil::toTextProto(*trace);
  } else if (format == envoy::config::tap::v3::OutputSink::PROTO_BINARY) {
    int size = trace->ByteSizeLong();
    json_string.resize(size);
    if (!trace->SerializeToArray(&json_string[0], size)) {
      return;
    }
  } else {
    json_string = MessageUtil::getJsonStringFromMessageOrError(*trace, true, false);
  }
  Buffer::OwnedImpl udp_data(std::move(json_string));
  Api::IoCallUint64Result write_result =
      parent_.udp_packet_writer_->writePacket(udp_data, nullptr, *parent_.udp_server_address_);
  if (!write_result.ok()) {
    ENVOY_LOG_MISC(debug, "{}: Failed to send UDP packet!", __func__);
  }
}

void UdpTapSink::UdpTapSinkHandle::submitTrace(TapCommon::TraceWrapperPtr&& trace,
                                               envoy::config::tap::v3::OutputSink::Format format) {
  switch (format) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED:
    // will implement above format if it is needed.
    ENVOY_LOG_MISC(debug, "{}: Not support PROTO_BINARY_LENGTH_DELIMITEDT", __func__);
    break;
  case envoy::config::tap::v3::OutputSink::PROTO_TEXT:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING: {
    if (!parent_.isUdpPacketWriterCreated()) {
      ENVOY_LOG_MISC(debug, "{}: udp writter isn't created yet", __func__);
      break;
    }
    // Currently, only large UDP messages (> 64KB) are handled for transport-streamed trace.
    // Support for other types of traces will be added as needed in the future.
    if (trace->has_socket_streamed_trace_segment()) {
      handleSocketStreamedTrace(std::move(trace), format);
    } else {
      doSubmitTrace(std::move(trace), format);
    }
  } break;
  }
}

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
