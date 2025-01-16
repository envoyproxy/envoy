#pragma once

#include "envoy/config/tap/v3/common.pb.h"

#include "source/common/network/socket_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/common/tap/tap.h"

#include "contrib/envoy/extensions/tap_sinks/udp_sink/v3alpha/udp_sink.pb.h"

namespace Envoy {
namespace Extensions {
namespace TapSinks {
namespace UDP {

namespace TapCommon = Extensions::Common::Tap;

class UdpTapSink : public TapCommon::Sink {
public:
  UdpTapSink(const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink& config);
  ~UdpTapSink();

  // Sink
  TapCommon::PerTapSinkHandlePtr
  createPerTapSinkHandle(uint64_t trace_id,
                         envoy::config::tap::v3::OutputSink::OutputSinkTypeCase) override {
    return std::make_unique<UdpTapSinkHandle>(*this, trace_id);
  }
  bool isUdpPacketWriterCreated(void) { return (udp_packet_writer_ != nullptr); }

private:
  struct UdpTapSinkHandle : public TapCommon::PerTapSinkHandle {
    UdpTapSinkHandle(UdpTapSink& parent, uint64_t trace_id)
        : parent_(parent), trace_id_(trace_id) {}

    // PerTapSinkHandle
    void submitTrace(TapCommon::TraceWrapperPtr&& trace,
                     envoy::config::tap::v3::OutputSink::Format format) override;

    UdpTapSink& parent_;
    const uint64_t trace_id_;
  };

  const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink config_;
  // Store the configured UDP address and port.
  Network::Address::InstanceConstSharedPtr udp_server_address_ = nullptr;
  // UDP client socket.
  Network::SocketPtr udp_socket_ = nullptr;

protected:
  // UDP client writer created with client socket.
  Network::UdpPacketWriterPtr udp_packet_writer_ = nullptr;
};

} // namespace UDP
} // namespace TapSinks
} // namespace Extensions
} // namespace Envoy
