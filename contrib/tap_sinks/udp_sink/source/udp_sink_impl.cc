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

  // Set sending buffer size if it is configured.
  if (PROTOBUF_GET_OPTIONAL_WRAPPED(config_, udp_sent_buffer_bytes) != absl::nullopt) {
    uint32_t configured_udp_sent_buffer_bytes = config_.udp_sent_buffer_bytes().value();
    if (configured_udp_sent_buffer_bytes > default_udp_send_buff_size_bytes) {
      // Ignore return value and use the system default if it is failed to set the value.
      (void)udp_socket_->setSocketOption(SOL_SOCKET, SO_SNDBUF, &configured_udp_sent_buffer_bytes,
                                         sizeof(configured_udp_sent_buffer_bytes));
      // Print the value set for debugging.
      uint32_t value_after_set = 0;
      socklen_t len_value_after_set = sizeof(value_after_set);
      udp_socket_->getSocketOption(SOL_SOCKET, SO_SNDBUF, &value_after_set, &len_value_after_set);
      ENVOY_LOG_MISC(debug, "{}: the UDP client send buff size {} after setting", __func__,
                     value_after_set);
    }
  }

  if (PROTOBUF_GET_OPTIONAL_WRAPPED(config_, udp_max_send_msg_size_bytes) != absl::nullopt) {
    uint32_t configured_udp_max_send_msg_size_bytes = config_.udp_max_send_msg_size_bytes().value();
    if (configured_udp_max_send_msg_size_bytes > 0) {
      // Overwrite the default value with configured value.
      udp_max_send_msg_size_bytes_ = configured_udp_max_send_msg_size_bytes;
      ENVOY_LOG_MISC(debug, "{}: the max value of UDP client per transmission is {} after setting",
                     __func__, udp_max_send_msg_size_bytes_);
    }
  }

  // Create udp writer.
  udp_packet_writer_ = std::make_unique<Network::UdpDefaultWriter>(udp_socket_->ioHandle());
  ENVOY_LOG_MISC(trace, "{}: UDP packet writer is created", __func__);
}

UdpTapSink::~UdpTapSink() { ENVOY_LOG_MISC(trace, "{}: UDP UdpTapSink() is called", __func__); }

void UdpTapSink::UdpTapSinkHandle::setStreamedTraceUdpDataAndSendUdpMsg(
    int32_t new_trace_cnt,
    const envoy::data::tap::v3::SocketStreamedTraceSegment& src_streamed_trace, bool is_read_event,
    size_t copy_offset, size_t copy_total_bytes,
    envoy::config::tap::v3::OutputSink::Format format) {

  TapCommon::TraceWrapperPtr dst_trace = std::make_unique<envoy::data::tap::v3::TraceWrapper>();
  envoy::data::tap::v3::SocketStreamedTraceSegment& dst_streamed_trace =
      *dst_trace->mutable_socket_streamed_trace_segment();

  // Set data from original trace to new trace.
  dst_streamed_trace.set_trace_id(src_streamed_trace.trace_id());

  ProtobufWkt::Timestamp* dst_ts = dst_streamed_trace.mutable_event()->mutable_timestamp();
  dst_ts->CopyFrom(src_streamed_trace.event().timestamp());
  dst_ts->set_nanos(dst_ts->nanos() + new_trace_cnt);

  dst_streamed_trace.mutable_event()->mutable_connection()->CopyFrom(
      src_streamed_trace.event().connection());

  if (is_read_event) {
    dst_streamed_trace.mutable_event()->mutable_read()->mutable_data()->set_as_bytes(
        src_streamed_trace.event().read().data().as_bytes().data() + copy_offset, copy_total_bytes);
  } else {
    dst_streamed_trace.mutable_event()->mutable_write()->mutable_data()->set_as_bytes(
        src_streamed_trace.event().write().data().as_bytes().data() + copy_offset,
        copy_total_bytes);
  }

  // Send UDP message.
  std::string json_string;
  if (format == envoy::config::tap::v3::OutputSink::PROTO_BINARY) {
    int size = dst_trace->ByteSizeLong();
    json_string.resize(size);
    if (!dst_trace->SerializeToArray(&json_string[0], size)) {
      return;
    }
  } else {
    json_string = MessageUtil::getJsonStringFromMessageOrError(*dst_trace, true, false);
  }
  Buffer::OwnedImpl udp_data(std::move(json_string));
  (void)parent_.udp_packet_writer_->writePacket(udp_data, nullptr, *parent_.udp_server_address_);
}

void UdpTapSink::UdpTapSinkHandle::handleBigUdpMsg(
    TapCommon::TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format format) {

  // It is tramsport streamed single event trace.
  const envoy::data::tap::v3::SocketStreamedTraceSegment& src_streamed_trace =
      trace->socket_streamed_trace_segment();

  // Get total body size based on read/write event.
  size_t total_body_bytes = 0;
  bool is_read_event = false;
  if (src_streamed_trace.event().has_read()) {
    is_read_event = true;
    total_body_bytes = src_streamed_trace.event().read().data().as_bytes().size();
  } else {
    total_body_bytes = src_streamed_trace.event().write().data().as_bytes().size();
  }

  // Handle data type is Bytes.
  size_t remaining_size_of_udp_data = 0;
  size_t copy_offset = 0;
  int32_t new_trace_cnt = 0;
  size_t max_size_of_each_sub_udp_msg = static_cast<size_t>(parent_.getUdpMaxSendMsgSize());
  while (true) {
    new_trace_cnt++;

    setStreamedTraceUdpDataAndSendUdpMsg(new_trace_cnt, src_streamed_trace, is_read_event,
                                         copy_offset, max_size_of_each_sub_udp_msg, format);

    remaining_size_of_udp_data = total_body_bytes - new_trace_cnt * max_size_of_each_sub_udp_msg;
    copy_offset = new_trace_cnt * max_size_of_each_sub_udp_msg;
    if (remaining_size_of_udp_data == 0) {
      // no data left
      break;
    }

    if (remaining_size_of_udp_data < max_size_of_each_sub_udp_msg) {
      // The last part data, set and send
      new_trace_cnt++;
      setStreamedTraceUdpDataAndSendUdpMsg(new_trace_cnt, src_streamed_trace, is_read_event,
                                           copy_offset, remaining_size_of_udp_data, format);
      break;
    }
  }
}

bool UdpTapSink::UdpTapSinkHandle::shouldHandleBigUdpMessage(
    const envoy::config::tap::v3::OutputSink::Format format,
    const envoy::data::tap::v3::SocketEvent& event)

{

  if (format != envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES &&
      format != envoy::config::tap::v3::OutputSink::PROTO_BINARY) {
    return false;
  }

  size_t total_body_bytes = 0;
  if (event.has_read()) {
    total_body_bytes = event.read().data().as_bytes().size();
  } else {
    total_body_bytes = event.write().data().as_bytes().size();
  }

  return total_body_bytes > static_cast<size_t>(parent_.getUdpMaxSendMsgSize());
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
    // Handle big UDP message (size > 62K).
    // Only support streamed trace single event.
    // For streamed tace events, handle it before call submiting.
    // For other traces will do it based on needs in the future.
    if (trace->has_socket_streamed_trace_segment() &&
        trace->socket_streamed_trace_segment().has_event() &&
        shouldHandleBigUdpMessage(format, trace->socket_streamed_trace_segment().event())) {
      handleBigUdpMsg(std::move(trace), format);
      // return directly and trace will be released in above funtion.
      return;
    }
    std::string json_string;
    if (format == envoy::config::tap::v3::OutputSink::PROTO_TEXT) {
      json_string = MessageUtil::toTextProto(*trace);
    } else if (format == envoy::config::tap::v3::OutputSink::PROTO_BINARY) {
      int size = trace->ByteSizeLong();
      json_string.resize(size);
      if (!trace->SerializeToArray(&json_string[0], size)) {
        break;
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
  } break;
  }
}

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
