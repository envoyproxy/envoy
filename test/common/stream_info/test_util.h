#pragma once

#include "envoy/stream_info/stream_info.h"

#include "common/common/assert.h"
#include "common/stream_info/filter_state_impl.h"

#include "test/test_common/test_time.h"

namespace Envoy {

class TestStreamInfo : public StreamInfo::StreamInfo {
public:
  TestStreamInfo() {
    tm fake_time;
    memset(&fake_time, 0, sizeof(fake_time));
    fake_time.tm_year = 99; // tm < 1901-12-13 20:45:52 is not valid on osx
    fake_time.tm_mday = 1;
    start_time_ = std::chrono::system_clock::from_time_t(timegm(&fake_time));

    MonotonicTime now = timeSystem().monotonicTime();
    start_time_monotonic_ = now;
    end_time_ = now + std::chrono::milliseconds(3);
  }

  SystemTime startTime() const override { return start_time_; }
  MonotonicTime startTimeMonotonic() const override { return start_time_monotonic_; }

  void addBytesReceived(uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  uint64_t bytesReceived() const override { return 1; }
  absl::optional<Http::Protocol> protocol() const override { return protocol_; }
  void protocol(Http::Protocol protocol) override { protocol_ = protocol; }
  absl::optional<uint32_t> responseCode() const override { return response_code_; }
  void addBytesSent(uint64_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  uint64_t bytesSent() const override { return 2; }
  bool intersectResponseFlags(uint64_t response_flags) const override {
    return (response_flags_ & response_flags) != 0;
  }
  bool hasResponseFlag(Envoy::StreamInfo::ResponseFlag response_flag) const override {
    return response_flags_ & response_flag;
  }
  bool hasAnyResponseFlag() const override { return response_flags_ != 0; }
  void setResponseFlag(Envoy::StreamInfo::ResponseFlag response_flag) override {
    response_flags_ |= response_flag;
  }
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_host_ = host;
  }
  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override { return upstream_host_; }
  void setUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_local_address) override {
    upstream_local_address_ = upstream_local_address;
  }
  const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const override {
    return upstream_local_address_;
  }
  bool healthCheck() const override { return hc_request_; }
  void healthCheck(bool is_hc) override { hc_request_ = is_hc; }

  void setDownstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& downstream_local_address) override {
    downstream_local_address_ = downstream_local_address;
  }
  const Network::Address::InstanceConstSharedPtr& downstreamLocalAddress() const override {
    return downstream_local_address_;
  }
  void setDownstreamRemoteAddress(
      const Network::Address::InstanceConstSharedPtr& downstream_remote_address) override {
    downstream_remote_address_ = downstream_remote_address;
  }
  const Network::Address::InstanceConstSharedPtr& downstreamRemoteAddress() const override {
    return downstream_remote_address_;
  }

  const Router::RouteEntry* routeEntry() const override { return route_entry_; }

  absl::optional<std::chrono::nanoseconds>
  duration(const absl::optional<MonotonicTime>& time) const {
    if (!time) {
      return {};
    }

    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() -
                                                                start_time_monotonic_);
  }

  absl::optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived() const override {
    return duration(last_rx_byte_received_);
  }

  void onLastDownstreamRxByteReceived() override {
    last_rx_byte_received_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> firstUpstreamTxByteSent() const override {
    return duration(first_upstream_tx_byte_sent_);
  }

  void onFirstUpstreamTxByteSent() override {
    first_upstream_tx_byte_sent_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> lastUpstreamTxByteSent() const override {
    return duration(last_upstream_tx_byte_sent_);
  }

  void onLastUpstreamTxByteSent() override {
    last_upstream_tx_byte_sent_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived() const override {
    return duration(first_upstream_rx_byte_received_);
  }

  void onFirstUpstreamRxByteReceived() override {
    first_upstream_rx_byte_received_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived() const override {
    return duration(last_upstream_rx_byte_received_);
  }

  void onLastUpstreamRxByteReceived() override {
    last_upstream_rx_byte_received_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> firstDownstreamTxByteSent() const override {
    return duration(first_downstream_tx_byte_sent_);
  }

  void onFirstDownstreamTxByteSent() override {
    first_downstream_tx_byte_sent_ = timeSystem().monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent() const override {
    return duration(last_downstream_tx_byte_sent_);
  }

  void onLastDownstreamTxByteSent() override {
    last_downstream_tx_byte_sent_ = timeSystem().monotonicTime();
  }

  void onRequestComplete() override { end_time_ = timeSystem().monotonicTime(); }

  void resetUpstreamTimings() override {
    first_upstream_tx_byte_sent_ = absl::optional<MonotonicTime>{};
    last_upstream_tx_byte_sent_ = absl::optional<MonotonicTime>{};
    first_upstream_rx_byte_received_ = absl::optional<MonotonicTime>{};
    last_upstream_rx_byte_received_ = absl::optional<MonotonicTime>{};
  }

  absl::optional<std::chrono::nanoseconds> requestComplete() const override {
    return duration(end_time_);
  }

  envoy::api::v2::core::Metadata& dynamicMetadata() override { return metadata_; };
  const envoy::api::v2::core::Metadata& dynamicMetadata() const override { return metadata_; };

  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    (*metadata_.mutable_filter_metadata())[name].MergeFrom(value);
  };

  const Envoy::StreamInfo::FilterState& filterState() const override { return filter_state_; }
  Envoy::StreamInfo::FilterState& filterState() override { return filter_state_; }

  void setRequestedServerName(const absl::string_view requested_server_name) override {
    requested_server_name_ = std::string(requested_server_name);
  }

  const std::string& requestedServerName() const override { return requested_server_name_; }

  Event::TimeSystem& timeSystem() { return test_time_.timeSystem(); }

  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;

  absl::optional<MonotonicTime> last_rx_byte_received_;
  absl::optional<MonotonicTime> first_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> first_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> last_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> first_downstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_downstream_tx_byte_sent_;
  absl::optional<MonotonicTime> end_time_;

  absl::optional<Http::Protocol> protocol_{Http::Protocol::Http11};
  absl::optional<uint32_t> response_code_;
  uint64_t response_flags_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  bool hc_request_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  const Router::RouteEntry* route_entry_{};
  envoy::api::v2::core::Metadata metadata_{};
  Envoy::StreamInfo::FilterStateImpl filter_state_{};
  std::string requested_server_name_;
  DangerousDeprecatedTestTime test_time_;
};

} // namespace Envoy
