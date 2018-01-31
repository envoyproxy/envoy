#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/request_info/request_info.h"

#include "common/common/assert.h"

namespace Envoy {
namespace RequestInfo {

struct RequestInfoImpl : public RequestInfo {
  RequestInfoImpl()
      : start_time_(std::chrono::system_clock::now()),
        start_time_monotonic_(std::chrono::steady_clock::now()) {}

  RequestInfoImpl(Http::Protocol protocol) : RequestInfoImpl() { protocol_ = protocol; }

  const SystemTime& startTime() const override { return start_time_; }

  const MonotonicTime& startTimeMonotonic() const override { return start_time_monotonic_; }

  const Optional<MonotonicTime>& lastDownstreamRxByteReceived() const override {
    return last_rx_byte_received_;
  }

  void lastDownstreamRxByteReceived(MonotonicTime time) override {
    last_rx_byte_received_.value(time);
  }

  const Optional<MonotonicTime>& firstUpstreamTxByteSent() const override {
    return first_upstream_tx_byte_sent_;
  }

  void firstUpstreamTxByteSent(MonotonicTime time) override {
    first_upstream_tx_byte_sent_.value(time);
  }

  const Optional<MonotonicTime>& lastUpstreamTxByteSent() const override {
    return last_upstream_tx_byte_sent_;
  }

  void lastUpstreamTxByteSent(MonotonicTime time) override {
    last_upstream_tx_byte_sent_.value(time);
  }

  const Optional<MonotonicTime>& firstUpstreamRxByteReceived() const override {
    return first_upstream_rx_byte_received_;
  }

  void firstUpstreamRxByteReceived(MonotonicTime time) override {
    first_upstream_rx_byte_received_.value(time);
  }

  const Optional<MonotonicTime>& lastUpstreamRxByteReceived() const override {
    return last_upstream_rx_byte_received_;
  }

  void lastUpstreamRxByteReceived(MonotonicTime time) override {
    last_upstream_rx_byte_received_.value(time);
  }

  const Optional<MonotonicTime>& firstDownstreamTxByteSent() const override {
    return first_downstream_tx_byte_sent_;
  }

  void firstDownstreamTxByteSent(MonotonicTime time) override {
    first_downstream_tx_byte_sent_.value(time);
  }

  const Optional<MonotonicTime>& lastDownstreamTxByteSent() const override {
    return last_downstream_tx_byte_sent_;
  }

  void lastDownstreamTxByteSent(MonotonicTime time) override {
    last_downstream_tx_byte_sent_.value(time);
  }

  void finalize(MonotonicTime time) override { end_time_.value(time); }

  const Optional<MonotonicTime>& finalTimeMonotonic() const override { return end_time_; }

  uint64_t bytesReceived() const override { return bytes_received_; }

  const Optional<Http::Protocol>& protocol() const override { return protocol_; }

  void protocol(Http::Protocol protocol) override { protocol_ = protocol; }

  const Optional<uint32_t>& responseCode() const override { return response_code_; }

  uint64_t bytesSent() const override { return bytes_sent_; }

  void setResponseFlag(ResponseFlag response_flag) override { response_flags_ |= response_flag; }

  bool getResponseFlag(ResponseFlag flag) const override { return response_flags_ & flag; }

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_host_ = host;
  }

  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override { return upstream_host_; }

  const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const override {
    return upstream_local_address_;
  }

  bool healthCheck() const override { return hc_request_; }

  void healthCheck(bool is_hc) override { hc_request_ = is_hc; }

  const Network::Address::InstanceConstSharedPtr& downstreamLocalAddress() const override {
    return downstream_local_address_;
  }

  const Network::Address::InstanceConstSharedPtr& downstreamRemoteAddress() const override {
    return downstream_remote_address_;
  }

  const Router::RouteEntry* routeEntry() const override { return route_entry_; }
  const SystemTime start_time_;
  const MonotonicTime start_time_monotonic_;

  Optional<MonotonicTime> last_rx_byte_received_;
  Optional<MonotonicTime> first_upstream_tx_byte_sent_;
  Optional<MonotonicTime> last_upstream_tx_byte_sent_;
  Optional<MonotonicTime> first_upstream_rx_byte_received_;
  Optional<MonotonicTime> last_upstream_rx_byte_received_;
  Optional<MonotonicTime> first_downstream_tx_byte_sent_;
  Optional<MonotonicTime> last_downstream_tx_byte_sent_;
  Optional<MonotonicTime> end_time_;

  Optional<Http::Protocol> protocol_;
  uint64_t bytes_received_{};
  Optional<uint32_t> response_code_;
  uint64_t bytes_sent_{};
  uint64_t response_flags_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  bool hc_request_{};
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  const Router::RouteEntry* route_entry_{};
};

} // namespace RequestInfo
} // namespace Envoy
