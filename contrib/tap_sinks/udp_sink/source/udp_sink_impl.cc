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

void UdpTapSink::UdpTapSinkHandle::submitTrace(TapCommon::TraceWrapperPtr&& trace,
                                               envoy::config::tap::v3::OutputSink::Format format) {
  switch (format) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::PROTO_BINARY_LENGTH_DELIMITED:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::PROTO_TEXT:
    // will implement above format if it is needed.
    ENVOY_LOG_MISC(debug,
                   "{}: Not support PROTO_BINARY, PROTO_BINARY_LENGTH_DELIMITED,  PROTO_TEXT",
                   __func__);
    break;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES:
    FALLTHRU;
  case envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING: {
    if (!parent_.isUdpPacketWriterCreated()) {
      ENVOY_LOG_MISC(debug, "{}: udp writter isn't created yet", __func__);
      break;
    }
    std::string json_string = MessageUtil::getJsonStringFromMessageOrError(*trace, true, false);
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
