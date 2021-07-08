#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/network/socket.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_reason.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/macros.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace StreamInfo {

namespace {

using ReplacementMap = absl::flat_hash_map<std::string, std::string>;

const ReplacementMap& emptySpaceReplacement() {
  CONSTRUCT_ON_FIRST_USE(
      ReplacementMap,
      ReplacementMap{{" ", "_"}, {"\t", "_"}, {"\f", "_"}, {"\v", "_"}, {"\n", "_"}, {"\r", "_"}});
}

} // namespace

struct StreamInfoImpl : public StreamInfo {
  StreamInfoImpl(TimeSource& time_source,
                 const Network::SocketAddressProviderSharedPtr& downstream_address_provider,
                 FilterState::LifeSpan life_span = FilterState::LifeSpan::FilterChain)
      : StreamInfoImpl(absl::nullopt, time_source, downstream_address_provider,
                       std::make_shared<FilterStateImpl>(life_span)) {}

  StreamInfoImpl(Http::Protocol protocol, TimeSource& time_source,
                 const Network::SocketAddressProviderSharedPtr& downstream_address_provider)
      : StreamInfoImpl(protocol, time_source, downstream_address_provider,
                       std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain)) {}

  StreamInfoImpl(Http::Protocol protocol, TimeSource& time_source,
                 const Network::SocketAddressProviderSharedPtr& downstream_address_provider,
                 FilterStateSharedPtr parent_filter_state, FilterState::LifeSpan life_span)
      : StreamInfoImpl(
            protocol, time_source, downstream_address_provider,
            std::make_shared<FilterStateImpl>(
                FilterStateImpl::LazyCreateAncestor(std::move(parent_filter_state), life_span),
                FilterState::LifeSpan::FilterChain)) {}

  SystemTime startTime() const override { return start_time_; }

  MonotonicTime startTimeMonotonic() const override { return start_time_monotonic_; }

  absl::optional<std::chrono::nanoseconds> duration(absl::optional<MonotonicTime> time) const {
    if (!time) {
      return {};
    }

    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() -
                                                                start_time_monotonic_);
  }

  absl::optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived() const override {
    return duration(last_downstream_rx_byte_received);
  }

  void onLastDownstreamRxByteReceived() override {
    ASSERT(!last_downstream_rx_byte_received);
    last_downstream_rx_byte_received = time_source_.monotonicTime();
  }

  void setUpstreamTiming(const UpstreamTiming& upstream_timing) override {
    upstream_timing_ = upstream_timing;
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
    ASSERT(!first_downstream_tx_byte_sent_);
    first_downstream_tx_byte_sent_ = time_source_.monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent() const override {
    return duration(last_downstream_tx_byte_sent_);
  }

  void onLastDownstreamTxByteSent() override {
    ASSERT(!last_downstream_tx_byte_sent_);
    last_downstream_tx_byte_sent_ = time_source_.monotonicTime();
  }

  absl::optional<std::chrono::nanoseconds> requestComplete() const override {
    return duration(final_time_);
  }

  void onRequestComplete() override {
    ASSERT(!final_time_);
    final_time_ = time_source_.monotonicTime();
  }

  void addBytesReceived(uint64_t bytes_received) override { bytes_received_ += bytes_received; }

  uint64_t bytesReceived() const override { return bytes_received_; }

  absl::optional<Http::Protocol> protocol() const override { return protocol_; }

  void protocol(Http::Protocol protocol) override { protocol_ = protocol; }

  absl::optional<uint32_t> responseCode() const override { return response_code_; }

  const absl::optional<std::string>& responseCodeDetails() const override {
    return response_code_details_;
  }

  void setResponseCode(uint32_t code) override { response_code_ = code; }

  void setResponseCodeDetails(absl::string_view rc_details) override {
    response_code_details_.emplace(absl::StrReplaceAll(rc_details, emptySpaceReplacement()));
  }

  const absl::optional<std::string>& connectionTerminationDetails() const override {
    return connection_termination_details_;
  }

  void setConnectionTerminationDetails(absl::string_view connection_termination_details) override {
    connection_termination_details_.emplace(connection_termination_details);
  }

  void addBytesSent(uint64_t bytes_sent) override { bytes_sent_ += bytes_sent; }

  uint64_t bytesSent() const override { return bytes_sent_; }

  void setResponseFlag(ResponseFlag response_flag) override { response_flags_ |= response_flag; }

  bool intersectResponseFlags(uint64_t response_flags) const override {
    return (response_flags_ & response_flags) != 0;
  }

  bool hasResponseFlag(ResponseFlag flag) const override { return response_flags_ & flag; }

  bool hasAnyResponseFlag() const override { return response_flags_ != 0; }

  uint64_t responseFlags() const override { return response_flags_; }

  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_host_ = host;
  }

  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override { return upstream_host_; }

  void setRouteName(absl::string_view route_name) override {
    route_name_ = std::string(route_name);
  }

  const std::string& getRouteName() const override { return route_name_; }

  void setUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_local_address) override {
    upstream_local_address_ = upstream_local_address;
  }

  const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const override {
    return upstream_local_address_;
  }

  bool healthCheck() const override { return health_check_request_; }

  void healthCheck(bool is_health_check) override { health_check_request_ = is_health_check; }

  const Network::SocketAddressProvider& downstreamAddressProvider() const override {
    return *downstream_address_provider_;
  }

  void
  setDownstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& connection_info) override {
    downstream_ssl_info_ = connection_info;
  }

  Ssl::ConnectionInfoConstSharedPtr downstreamSslConnection() const override {
    return downstream_ssl_info_;
  }

  void setUpstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& connection_info) override {
    upstream_ssl_info_ = connection_info;
  }

  Ssl::ConnectionInfoConstSharedPtr upstreamSslConnection() const override {
    return upstream_ssl_info_;
  }

  const Router::RouteEntry* routeEntry() const override { return route_entry_; }

  envoy::config::core::v3::Metadata& dynamicMetadata() override { return metadata_; };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override { return metadata_; };

  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    (*metadata_.mutable_filter_metadata())[name].MergeFrom(value);
  };

  const FilterStateSharedPtr& filterState() override { return filter_state_; }
  const FilterState& filterState() const override { return *filter_state_; }

  const FilterStateSharedPtr& upstreamFilterState() const override {
    return upstream_filter_state_;
  }
  void setUpstreamFilterState(const FilterStateSharedPtr& filter_state) override {
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

  void dumpState(std::ostream& os, int indent_level = 0) const {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "StreamInfoImpl " << this << DUMP_OPTIONAL_MEMBER(protocol_)
       << DUMP_OPTIONAL_MEMBER(response_code_) << DUMP_OPTIONAL_MEMBER(response_code_details_)
       << DUMP_MEMBER(health_check_request_) << DUMP_MEMBER(route_name_) << "\n";
  }

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

  TimeSource& time_source_;
  const SystemTime start_time_;
  const MonotonicTime start_time_monotonic_;

  absl::optional<MonotonicTime> last_downstream_rx_byte_received;
  absl::optional<MonotonicTime> first_downstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_downstream_tx_byte_sent_;
  absl::optional<MonotonicTime> final_time_;

  absl::optional<Http::Protocol> protocol_;
  absl::optional<uint32_t> response_code_;
  absl::optional<std::string> response_code_details_;
  absl::optional<std::string> connection_termination_details_;
  uint64_t response_flags_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  bool health_check_request_{};
  const Router::RouteEntry* route_entry_{};
  envoy::config::core::v3::Metadata metadata_{};
  FilterStateSharedPtr filter_state_;
  FilterStateSharedPtr upstream_filter_state_;
  std::string route_name_;

private:
  static Network::SocketAddressProviderSharedPtr emptyDownstreamAddressProvider() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(
        Network::SocketAddressProviderSharedPtr,
        std::make_shared<Network::SocketAddressSetterImpl>(nullptr, nullptr));
  }

  StreamInfoImpl(absl::optional<Http::Protocol> protocol, TimeSource& time_source,
                 const Network::SocketAddressProviderSharedPtr& downstream_address_provider,
                 FilterStateSharedPtr filter_state)
      : time_source_(time_source), start_time_(time_source.systemTime()),
        start_time_monotonic_(time_source.monotonicTime()), protocol_(protocol),
        filter_state_(std::move(filter_state)),
        downstream_address_provider_(downstream_address_provider != nullptr
                                         ? downstream_address_provider
                                         : emptyDownstreamAddressProvider()),
        trace_reason_(Tracing::Reason::NotTraceable) {}

  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  const Network::SocketAddressProviderSharedPtr downstream_address_provider_;
  Ssl::ConnectionInfoConstSharedPtr downstream_ssl_info_;
  Ssl::ConnectionInfoConstSharedPtr upstream_ssl_info_;
  std::string requested_server_name_;
  const Http::RequestHeaderMap* request_headers_{};
  Http::RequestIdStreamInfoProviderSharedPtr request_id_provider_;
  UpstreamTiming upstream_timing_;
  std::string upstream_transport_failure_reason_;
  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstream_cluster_info_;
  std::string filter_chain_name_;
  Tracing::Reason trace_reason_;
};

} // namespace StreamInfo
} // namespace Envoy
