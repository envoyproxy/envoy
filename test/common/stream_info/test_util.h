#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/request_id/uuid/config.h"

#include "test/test_common/simulated_time_system.h"

namespace Envoy {

class TestStreamInfo : public StreamInfo::StreamInfo {
public:
  TestStreamInfo()
      : filter_state_(std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
            Envoy::StreamInfo::FilterState::LifeSpan::FilterChain)) {
    // Use 1999-01-01 00:00:00 +0
    time_t fake_time = 915148800;
    start_time_ = std::chrono::system_clock::from_time_t(fake_time);
    request_id_provider_ = Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(random_);

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
  const absl::optional<std::string>& responseCodeDetails() const override {
    return response_code_details_;
  }
  void setResponseCode(uint32_t code) override { response_code_ = code; }
  void setResponseCodeDetails(absl::string_view rc_details) override {
    response_code_details_.emplace(rc_details);
  }
  const absl::optional<std::string>& connectionTerminationDetails() const override {
    return connection_termination_details_;
  }
  void setConnectionTerminationDetails(absl::string_view details) override {
    connection_termination_details_.emplace(details);
  }
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
  uint64_t responseFlags() const override { return response_flags_; }
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
  bool healthCheck() const override { return health_check_request_; }
  void healthCheck(bool is_health_check) override { health_check_request_ = is_health_check; }
  const Network::SocketAddressSetter& downstreamAddressProvider() const override {
    return *downstream_address_provider_;
  }

  void setUpstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& connection_info) override {
    upstream_connection_info_ = connection_info;
  }

  Ssl::ConnectionInfoConstSharedPtr upstreamSslConnection() const override {
    return upstream_connection_info_;
  }
  void setRouteName(absl::string_view route_name) override {
    route_name_ = std::string(route_name);
  }
  const std::string& getRouteName() const override { return route_name_; }

  Router::RouteConstSharedPtr route() const override { return route_; }

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
    return duration(upstream_timing_.first_upstream_tx_byte_sent_);
  }

  absl::optional<std::chrono::nanoseconds> lastUpstreamTxByteSent() const override {
    return duration(upstream_timing_.last_upstream_tx_byte_sent_);
  }
  absl::optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived() const override {
    return duration(upstream_timing_.first_upstream_rx_byte_received_);
  }

  absl::optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived() const override {
    return duration(upstream_timing_.last_upstream_rx_byte_received_);
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

  void setUpstreamTiming(const Envoy::StreamInfo::UpstreamTiming& upstream_timing) override {
    upstream_timing_ = upstream_timing;
  }

  absl::optional<std::chrono::nanoseconds> requestComplete() const override {
    return duration(end_time_);
  }

  envoy::config::core::v3::Metadata& dynamicMetadata() override { return metadata_; };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override { return metadata_; };

  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    (*metadata_.mutable_filter_metadata())[name].MergeFrom(value);
  };

  const Envoy::StreamInfo::FilterStateSharedPtr& filterState() override { return filter_state_; }
  const Envoy::StreamInfo::FilterState& filterState() const override { return *filter_state_; }

  const Envoy::StreamInfo::FilterStateSharedPtr& upstreamFilterState() const override {
    return upstream_filter_state_;
  }
  void
  setUpstreamFilterState(const Envoy::StreamInfo::FilterStateSharedPtr& filter_state) override {
    upstream_filter_state_ = filter_state;
  }

  void setUpstreamTransportFailureReason(absl::string_view failure_reason) override {
    upstream_transport_failure_reason_ = std::string(failure_reason);
  }

  const std::string& upstreamTransportFailureReason() const override {
    return upstream_transport_failure_reason_;
  }

  void setRequestHeaders(const Http::RequestHeaderMap& headers) override {
    request_headers_ = &headers;
  }

  const Http::RequestHeaderMap* getRequestHeaders() const override { return request_headers_; }

  void setRequestIDProvider(const Http::RequestIdStreamInfoProviderSharedPtr& provider) override {
    ASSERT(provider != nullptr);
    request_id_provider_ = provider;
  }
  const Http::RequestIdStreamInfoProvider* getRequestIDProvider() const override {
    return request_id_provider_.get();
  }

  void setTraceReason(Tracing::Reason reason) override { trace_reason_ = reason; }
  Tracing::Reason traceReason() const override { return trace_reason_; }

  Event::TimeSystem& timeSystem() { return test_time_.timeSystem(); }

  void setUpstreamClusterInfo(
      const Upstream::ClusterInfoConstSharedPtr& upstream_cluster_info) override {
    upstream_cluster_info_ = upstream_cluster_info;
  }
  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstreamClusterInfo() const override {
    return upstream_cluster_info_;
  }

  void setFilterChainName(absl::string_view filter_chain_name) override {
    filter_chain_name_ = std::string(filter_chain_name);
  }

  const std::string& filterChainName() const override { return filter_chain_name_; }

  void setUpstreamConnectionId(uint64_t id) override { upstream_connection_id_ = id; }

  absl::optional<uint64_t> upstreamConnectionId() const override { return upstream_connection_id_; }

  void setAttemptCount(uint32_t attempt_count) override { attempt_count_ = attempt_count; }

  absl::optional<uint32_t> attemptCount() const override { return attempt_count_; }

  Random::RandomGeneratorImpl random_;
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
  absl::optional<std::string> response_code_details_;
  absl::optional<std::string> connection_termination_details_;
  uint64_t response_flags_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  bool health_check_request_{};
  std::string route_name_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::SocketAddressSetterSharedPtr downstream_address_provider_{
      std::make_shared<Network::SocketAddressSetterImpl>(nullptr, nullptr)};
  Ssl::ConnectionInfoConstSharedPtr downstream_connection_info_;
  Ssl::ConnectionInfoConstSharedPtr upstream_connection_info_;
  Router::RouteConstSharedPtr route_;
  envoy::config::core::v3::Metadata metadata_{};
  Envoy::StreamInfo::FilterStateSharedPtr filter_state_{
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain)};
  Envoy::StreamInfo::FilterStateSharedPtr upstream_filter_state_;
  Envoy::StreamInfo::UpstreamTiming upstream_timing_;
  std::string requested_server_name_;
  std::string upstream_transport_failure_reason_;
  const Http::RequestHeaderMap* request_headers_{};
  Envoy::Event::SimulatedTimeSystem test_time_;
  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstream_cluster_info_{};
  Http::RequestIdStreamInfoProviderSharedPtr request_id_provider_;
  std::string filter_chain_name_;
  Tracing::Reason trace_reason_{Tracing::Reason::NotTraceable};
  absl::optional<uint64_t> upstream_connection_id_;
  absl::optional<uint32_t> attempt_count_;
};

} // namespace Envoy
