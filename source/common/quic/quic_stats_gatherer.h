#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "quiche/quic/core/quic_ack_listener_interface.h"

namespace Envoy {
namespace Quic {

class QuicStatsGatherer : public quic::QuicAckListenerInterface {
public:
  void OnPacketAcked(int acked_bytes, quic::QuicTime::Delta delta_largest_observed) override;
  void OnPacketRetransmitted(int /* retransmitted_bytes */) override {}
  void addBytesSent(uint64_t bytes_sent, bool end_stream) {
    bytes_outstanding_ += bytes_sent;
    fin_sent_ = end_stream;
  }
  void doDeferredLog();
  void addStreamInfo(std::shared_ptr<StreamInfo::StreamInfo> stream_info) {
    if (stream_info != nullptr) {
      stream_info_.push_back(stream_info);
    }
  }
  void setAccessLogHandlers(std::list<AccessLog::InstanceSharedPtr> handlers) {
    access_log_handlers_ = handlers;
  }
  void setTimeSource(Envoy::TimeSource* time_source) { time_source_ = time_source; }

private:
  uint64_t bytes_outstanding_ = 0;
  bool fin_sent_ = false;
  std::list<std::shared_ptr<StreamInfo::StreamInfo>> stream_info_{};
  std::list<AccessLog::InstanceSharedPtr> access_log_handlers_{};
  Envoy::TimeSource* time_source_ = nullptr;
  bool logging_done_ = false;
};

} // namespace Quic
} // namespace Envoy
