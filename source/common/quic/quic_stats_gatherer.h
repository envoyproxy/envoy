#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "quiche/quic/core/quic_ack_listener_interface.h"

namespace Envoy {
namespace Quic {

// Ack listener that stores access logging information and performs
// logging after the final ack.
class QuicStatsGatherer : public quic::QuicAckListenerInterface {
public:
  explicit QuicStatsGatherer(Envoy::TimeSource* time_source) : time_source_(time_source) {}

  // QuicAckListenerInterface
  void OnPacketAcked(int acked_bytes, quic::QuicTime::Delta delta_largest_observed) override;
  void OnPacketRetransmitted(int /* retransmitted_bytes */) override {}

  // Add bytes sent for this stream, for internal tracking of bytes acked.
  void addBytesSent(uint64_t bytes_sent, bool end_stream) {
    bytes_outstanding_ += bytes_sent;
    fin_sent_ = end_stream;
  }
  // Log this stream using available stream info and access loggers.
  void maybeDoDeferredLog();
  // Add a pointer to stream info that should be logged. This can be overwritten
  // in case of an internal redirect.
  void setStreamInfo(std::shared_ptr<StreamInfo::StreamInfo> stream_info) {
    stream_info_ = stream_info;
  }
  // Set list of pointers to access loggers.
  void setAccessLogHandlers(std::list<AccessLog::InstanceSharedPtr> handlers) {
    access_log_handlers_ = handlers;
  }

private:
  uint64_t bytes_outstanding_ = 0;
  bool fin_sent_ = false;
  std::shared_ptr<StreamInfo::StreamInfo> stream_info_ = nullptr;
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_{};
  Envoy::TimeSource* time_source_ = nullptr;
  bool logging_done_ = false;
};

} // namespace Quic
} // namespace Envoy
