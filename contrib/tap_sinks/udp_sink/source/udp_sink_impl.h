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
  ~UdpTapSink() override;

  // Sink
  TapCommon::PerTapSinkHandlePtr
  createPerTapSinkHandle(uint64_t trace_id,
                         envoy::config::tap::v3::OutputSink::OutputSinkTypeCase) override {
    return std::make_unique<UdpTapSinkHandle>(*this, trace_id);
  }
  bool isUdpPacketWriterCreated(void) { return (udp_packet_writer_ != nullptr); }
  uint32_t getUdpMaxSendMsgDataSize(const envoy::config::tap::v3::OutputSink::Format format) const;
  void setUdpMaxSendMsgDataSizeAsBytes(uint32_t data_size) {
    udp_max_send_msg_size_bytes_ = data_size;
  }
  void setUdpMaxSendMsgDataSizeAsString(uint32_t data_size) {
    udp_max_send_msg_size_string_ = data_size;
  }

private:
  struct UdpTapSinkHandle : public TapCommon::PerTapSinkHandle {
    UdpTapSinkHandle(UdpTapSink& parent, uint64_t trace_id)
        : parent_(parent), trace_id_(trace_id) {}

    // PerTapSinkHandle
    void submitTrace(TapCommon::TraceWrapperPtr&& trace,
                     envoy::config::tap::v3::OutputSink::Format format) override;
    void doSubmitTrace(TapCommon::TraceWrapperPtr&& trace,
                       envoy::config::tap::v3::OutputSink::Format format);
    void setStreamedTraceDataAndSubmit(
        int32_t new_trace_cnt,
        const envoy::data::tap::v3::SocketStreamedTraceSegment& src_streamed_trace,
        bool is_read_event, size_t copy_offset, size_t copy_total_bytes,
        envoy::config::tap::v3::OutputSink::Format format);
    void handleSocketStreamedTrace(TapCommon::TraceWrapperPtr&& trace,
                                   envoy::config::tap::v3::OutputSink::Format format);
    size_t getEventBodysize(const envoy::data::tap::v3::SocketEvent& event,
                            envoy::config::tap::v3::OutputSink::Format format);
    size_t getEventSize(const envoy::data::tap::v3::SocketEvent& event,
                        envoy::config::tap::v3::OutputSink::Format format);
    void handleSocketStreamedTraceForMMultiEventsBigBody(
        envoy::config::tap::v3::OutputSink::Format format,
        const envoy::data::tap::v3::SocketEvent& event, uint64_t trace_id);
    void handleSocketStreamedTraceForMultiEvents(TapCommon::TraceWrapperPtr&& trace,
                                                 envoy::config::tap::v3::OutputSink::Format format);

    UdpTapSink& parent_;
    const uint64_t trace_id_;
  };

  const envoy::extensions::tap_sinks::udp_sink::v3alpha::UdpSink config_;
  // Store the configured UDP address and port.
  Network::Address::InstanceConstSharedPtr udp_server_address_ = nullptr;

  // Max sending msg data size per UDP transmission when data type is string.
  uint32_t udp_max_send_msg_size_string_{63488};
  // Max sending msg data size per UDP transmission when data type is bytes.
  uint32_t udp_max_send_msg_size_bytes_{47104};

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
