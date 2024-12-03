#pragma once

#include <chrono>
#include <cstdint>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/network/socket.h"
#include "envoy/router/router.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/trace_reason.h"

#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/network/socket_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace StreamInfo {

struct UpstreamInfoImpl : public UpstreamInfo {
  void setUpstreamConnectionId(uint64_t id) override { upstream_connection_id_ = id; }

  absl::optional<uint64_t> upstreamConnectionId() const override { return upstream_connection_id_; }

  void setUpstreamInterfaceName(absl::string_view interface_name) override {
    upstream_connection_interface_name_ = std::string(interface_name);
  }

  absl::optional<absl::string_view> upstreamInterfaceName() const override {
    return upstream_connection_interface_name_;
  }

  void
  setUpstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info) override {
    upstream_ssl_info_ = ssl_connection_info;
  }

  Ssl::ConnectionInfoConstSharedPtr upstreamSslConnection() const override {
    return upstream_ssl_info_;
  }
  UpstreamTiming& upstreamTiming() override { return upstream_timing_; }
  const UpstreamTiming& upstreamTiming() const override { return upstream_timing_; }
  const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const override {
    return upstream_local_address_;
  }
  const Network::Address::InstanceConstSharedPtr& upstreamRemoteAddress() const override {
    return upstream_remote_address_;
  }
  void setUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_local_address) override {
    upstream_local_address_ = upstream_local_address;
  }
  void setUpstreamRemoteAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_remote_address) override {
    upstream_remote_address_ = upstream_remote_address;
  }
  void setUpstreamTransportFailureReason(absl::string_view failure_reason) override {
    upstream_transport_failure_reason_ = std::string(failure_reason);
  }
  const std::string& upstreamTransportFailureReason() const override {
    return upstream_transport_failure_reason_;
  }
  void setUpstreamHost(Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_host_ = host;
  }
  const FilterStateSharedPtr& upstreamFilterState() const override {
    return upstream_filter_state_;
  }
  void setUpstreamFilterState(const FilterStateSharedPtr& filter_state) override {
    upstream_filter_state_ = filter_state;
  }

  Upstream::HostDescriptionConstSharedPtr upstreamHost() const override { return upstream_host_; }

  void dumpState(std::ostream& os, int indent_level = 0) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "UpstreamInfoImpl " << this << DUMP_OPTIONAL_MEMBER(upstream_connection_id_)
       << "\n";
  }
  void setUpstreamNumStreams(uint64_t num_streams) override { num_streams_ = num_streams; }
  uint64_t upstreamNumStreams() const override { return num_streams_; }

  void setUpstreamProtocol(Http::Protocol protocol) override { upstream_protocol_ = protocol; }
  absl::optional<Http::Protocol> upstreamProtocol() const override { return upstream_protocol_; }

  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  UpstreamTiming upstream_timing_;
  Ssl::ConnectionInfoConstSharedPtr upstream_ssl_info_;
  absl::optional<uint64_t> upstream_connection_id_;
  absl::optional<std::string> upstream_connection_interface_name_;
  std::string upstream_transport_failure_reason_;
  FilterStateSharedPtr upstream_filter_state_;
  size_t num_streams_{};
  absl::optional<Http::Protocol> upstream_protocol_;
};

struct StreamInfoImpl : public StreamInfo {
  StreamInfoImpl(
      TimeSource& time_source,
      const Network::ConnectionInfoProviderSharedPtr& downstream_connection_info_provider,
      FilterState::LifeSpan life_span, FilterStateSharedPtr ancestor_filter_state = nullptr)
      : StreamInfoImpl(
            absl::nullopt, time_source, downstream_connection_info_provider,
            std::make_shared<FilterStateImpl>(std::move(ancestor_filter_state), life_span)) {}

  StreamInfoImpl(
      Http::Protocol protocol, TimeSource& time_source,
      const Network::ConnectionInfoProviderSharedPtr& downstream_connection_info_provider,
      FilterState::LifeSpan life_span, FilterStateSharedPtr ancestor_filter_state = nullptr)
      : StreamInfoImpl(
            protocol, time_source, downstream_connection_info_provider,
            std::make_shared<FilterStateImpl>(std::move(ancestor_filter_state), life_span)) {}

  StreamInfoImpl(
      absl::optional<Http::Protocol> protocol, TimeSource& time_source,
      const Network::ConnectionInfoProviderSharedPtr& downstream_connection_info_provider,
      FilterStateSharedPtr filter_state)
      : time_source_(time_source), start_time_(time_source.systemTime()),
        start_time_monotonic_(time_source.monotonicTime()), protocol_(protocol),
        filter_state_(std::move(filter_state)),
        downstream_connection_info_provider_(downstream_connection_info_provider != nullptr
                                                 ? downstream_connection_info_provider
                                                 : emptyDownstreamAddressProvider()),
        trace_reason_(Tracing::Reason::NotTraceable) {}

  SystemTime startTime() const override { return start_time_; }

  MonotonicTime startTimeMonotonic() const override { return start_time_monotonic_; }

  TimeSource& timeSource() const override { return time_source_; }

  absl::optional<std::chrono::nanoseconds> duration(absl::optional<MonotonicTime> time) const {
    if (!time) {
      return {};
    }

    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.value() -
                                                                start_time_monotonic_);
  }

  void setUpstreamInfo(std::shared_ptr<UpstreamInfo> info) override { upstream_info_ = info; }

  std::shared_ptr<UpstreamInfo> upstreamInfo() override { return upstream_info_; }

  OptRef<const UpstreamInfo> upstreamInfo() const override {
    if (!upstream_info_) {
      return {};
    }
    return *upstream_info_;
  }

  absl::optional<std::chrono::nanoseconds> currentDuration() const override {
    if (!final_time_) {
      return duration(time_source_.monotonicTime());
    }

    return requestComplete();
  }

  absl::optional<std::chrono::nanoseconds> requestComplete() const override {
    return duration(final_time_);
  }

  void onRequestComplete() override {
    ASSERT(!final_time_);
    final_time_ = time_source_.monotonicTime();
  }

  DownstreamTiming& downstreamTiming() override {
    if (!downstream_timing_.has_value()) {
      downstream_timing_ = DownstreamTiming();
    }
    return downstream_timing_.value();
  }
  OptRef<const DownstreamTiming> downstreamTiming() const override {
    if (!downstream_timing_.has_value()) {
      return {};
    }
    return {*downstream_timing_};
  }

  void addBytesReceived(uint64_t bytes_received) override { bytes_received_ += bytes_received; }

  uint64_t bytesReceived() const override { return bytes_received_; }

  void addBytesRetransmitted(uint64_t bytes_retransmitted) override {
    bytes_retransmitted_ += bytes_retransmitted;
  }

  uint64_t bytesRetransmitted() const override { return bytes_retransmitted_; }

  void addPacketsRetransmitted(uint64_t packets_retransmitted) override {
    packets_retransmitted_ += packets_retransmitted;
  }

  uint64_t packetsRetransmitted() const override { return packets_retransmitted_; }

  absl::optional<Http::Protocol> protocol() const override { return protocol_; }

  void protocol(Http::Protocol protocol) override { protocol_ = protocol; }

  absl::optional<uint32_t> responseCode() const override { return response_code_; }

  const absl::optional<std::string>& responseCodeDetails() const override {
    return response_code_details_;
  }

  void setResponseCode(uint32_t code) override { response_code_ = code; }

  void setResponseCodeDetails(absl::string_view rc_details) override {
    // Callers should sanitize with StringUtil::replaceAllEmptySpace if necessary.
    ASSERT(!StringUtil::hasEmptySpace(rc_details));
    response_code_details_.emplace(rc_details);
  }

  const absl::optional<std::string>& connectionTerminationDetails() const override {
    return connection_termination_details_;
  }

  void setConnectionTerminationDetails(absl::string_view connection_termination_details) override {
    connection_termination_details_.emplace(connection_termination_details);
  }

  void addBytesSent(uint64_t bytes_sent) override { bytes_sent_ += bytes_sent; }

  uint64_t bytesSent() const override { return bytes_sent_; }

  void setResponseFlag(ResponseFlag flag) override {
    ASSERT(flag.value() < ResponseFlagUtils::responseFlagsVec().size());
    if (!hasResponseFlag(flag)) {
      response_flags_.push_back(flag);
    }
  }

  bool hasResponseFlag(ResponseFlag flag) const override {
    return std::find(response_flags_.begin(), response_flags_.end(), flag) != response_flags_.end();
  }

  bool hasAnyResponseFlag() const override { return !response_flags_.empty(); }

  absl::Span<const ResponseFlag> responseFlags() const override { return response_flags_; }

  uint64_t legacyResponseFlags() const override {
    uint64_t legacy_flags = 0;
    for (ResponseFlag flag : response_flags_) {
      if (flag.value() <= static_cast<uint16_t>(CoreResponseFlag::LastFlag)) {
        ASSERT(flag.value() < 64, "Legacy response flag out of range");
        legacy_flags |= (1UL << flag.value());
      }
    }
    return legacy_flags;
  }

  const std::string& getRouteName() const override {
    return route_ != nullptr ? route_->routeName() : EMPTY_STRING;
  }

  void setVirtualClusterName(const absl::optional<std::string>& virtual_cluster_name) override {
    virtual_cluster_name_ = virtual_cluster_name;
  }

  const absl::optional<std::string>& virtualClusterName() const override {
    return virtual_cluster_name_;
  }

  bool healthCheck() const override { return health_check_request_; }

  void healthCheck(bool is_health_check) override { health_check_request_ = is_health_check; }

  const Network::ConnectionInfoProvider& downstreamAddressProvider() const override {
    return *downstream_connection_info_provider_;
  }

  Router::RouteConstSharedPtr route() const override { return route_; }

  envoy::config::core::v3::Metadata& dynamicMetadata() override { return metadata_; };
  const envoy::config::core::v3::Metadata& dynamicMetadata() const override { return metadata_; };

  void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) override {
    (*metadata_.mutable_filter_metadata())[name].MergeFrom(value);
  };

  void setDynamicTypedMetadata(const std::string& name, const ProtobufWkt::Any& value) override {
    (*metadata_.mutable_typed_filter_metadata())[name].MergeFrom(value);
  }

  const FilterStateSharedPtr& filterState() override { return filter_state_; }
  const FilterState& filterState() const override { return *filter_state_; }

  void setRequestHeaders(const Http::RequestHeaderMap& headers) override {
    request_headers_ = &headers;
  }

  const Http::RequestHeaderMap* getRequestHeaders() const override { return request_headers_; }

  void setStreamIdProvider(StreamIdProviderSharedPtr provider) override {
    stream_id_provider_ = std::move(provider);
  }
  OptRef<const StreamIdProvider> getStreamIdProvider() const override {
    if (stream_id_provider_ == nullptr) {
      return {};
    }
    return makeOptRef<const StreamIdProvider>(*stream_id_provider_);
  }

  void setTraceReason(Tracing::Reason reason) override { trace_reason_ = reason; }
  Tracing::Reason traceReason() const override { return trace_reason_; }

  void dumpState(std::ostream& os, int indent_level = 0) const override {
    const char* spaces = spacesForLevel(indent_level);
    os << spaces << "StreamInfoImpl " << this << DUMP_OPTIONAL_MEMBER(protocol_)
       << DUMP_OPTIONAL_MEMBER(response_code_) << DUMP_OPTIONAL_MEMBER(response_code_details_)
       << DUMP_OPTIONAL_MEMBER(attempt_count_) << DUMP_MEMBER(health_check_request_)
       << DUMP_MEMBER(getRouteName());
    DUMP_DETAILS(upstream_info_);
  }

  void setUpstreamClusterInfo(
      const Upstream::ClusterInfoConstSharedPtr& upstream_cluster_info) override {
    upstream_cluster_info_ = upstream_cluster_info;
  }

  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstreamClusterInfo() const override {
    return upstream_cluster_info_;
  }

  void setAttemptCount(uint32_t attempt_count) override { attempt_count_ = attempt_count; }

  absl::optional<uint32_t> attemptCount() const override { return attempt_count_; }

  const BytesMeterSharedPtr& getUpstreamBytesMeter() const override {
    return upstream_bytes_meter_;
  }

  const BytesMeterSharedPtr& getDownstreamBytesMeter() const override {
    return downstream_bytes_meter_;
  }

  void setUpstreamBytesMeter(const BytesMeterSharedPtr& upstream_bytes_meter) override {
    upstream_bytes_meter->captureExistingBytesMeter(*upstream_bytes_meter_);
    upstream_bytes_meter_ = upstream_bytes_meter;
  }

  void setDownstreamBytesMeter(const BytesMeterSharedPtr& downstream_bytes_meter) override {
    // Downstream bytes counter don't reset during a retry.
    if (downstream_bytes_meter_ == nullptr) {
      downstream_bytes_meter_ = downstream_bytes_meter;
    }
    ASSERT(downstream_bytes_meter_.get() == downstream_bytes_meter.get());
  }

  // This function is used to persist relevant information from the original
  // stream into to the new one, when recreating the stream. Generally this
  // includes information about the downstream stream, but not the upstream
  // stream.
  void setFromForRecreateStream(StreamInfo& info) {
    downstream_timing_ = info.downstreamTiming();
    protocol_ = info.protocol();
    bytes_received_ = info.bytesReceived();
    downstream_bytes_meter_ = info.getDownstreamBytesMeter();
    // These two are set in the constructor, but to T(recreate), and should be T(create)
    start_time_ = info.startTime();
    start_time_monotonic_ = info.startTimeMonotonic();
    downstream_transport_failure_reason_ = std::string(info.downstreamTransportFailureReason());
    bytes_retransmitted_ = info.bytesRetransmitted();
    packets_retransmitted_ = info.packetsRetransmitted();
    should_drain_connection_ = info.shouldDrainConnectionUponCompletion();
  }

  // This function is used to copy over every field exposed in the StreamInfo interface, with a
  // couple of exceptions noted below. Note that setFromForRecreateStream is reused here.
  // * request_headers_ is a raw pointer; to avoid pointer lifetime issues, a request header pointer
  // is required to be passed in here.
  // * downstream_connection_info_provider_ is always set in the ctor.
  void setFrom(StreamInfo& info, const Http::RequestHeaderMap* request_headers) {
    setFromForRecreateStream(info);
    virtual_cluster_name_ = info.virtualClusterName();
    response_code_ = info.responseCode();
    response_code_details_ = info.responseCodeDetails();
    connection_termination_details_ = info.connectionTerminationDetails();
    upstream_info_ = info.upstreamInfo();
    if (info.requestComplete().has_value()) {
      // derive final time from other info's complete duration and start time.
      final_time_ = info.startTimeMonotonic() + info.requestComplete().value();
    }
    response_flags_.clear();
    auto other_response_flags = info.responseFlags();
    response_flags_.insert(response_flags_.end(), other_response_flags.begin(),
                           other_response_flags.end());
    health_check_request_ = info.healthCheck();
    route_ = info.route();
    metadata_ = info.dynamicMetadata();
    filter_state_ = info.filterState();
    request_headers_ = request_headers;
    upstream_cluster_info_ = info.upstreamClusterInfo();
    auto stream_id_provider = info.getStreamIdProvider();
    if (stream_id_provider.has_value() && stream_id_provider->toStringView().has_value()) {
      std::string id{stream_id_provider->toStringView().value()};
      stream_id_provider_ = std::make_shared<StreamIdProviderImpl>(std::move(id));
    }
    trace_reason_ = info.traceReason();
    attempt_count_ = info.attemptCount();
    upstream_bytes_meter_ = info.getUpstreamBytesMeter();
    bytes_sent_ = info.bytesSent();
    is_shadow_ = info.isShadow();
    parent_stream_info_ = info.parentStreamInfo();
  }

  void setIsShadow(bool is_shadow) { is_shadow_ = is_shadow; }
  bool isShadow() const override { return is_shadow_; }

  void setDownstreamTransportFailureReason(absl::string_view failure_reason) override {
    downstream_transport_failure_reason_ = std::string(failure_reason);
  }

  absl::string_view downstreamTransportFailureReason() const override {
    return downstream_transport_failure_reason_;
  }

  bool shouldSchemeMatchUpstream() const override { return should_scheme_match_upstream_; }

  void setShouldSchemeMatchUpstream(bool should_match_upstream) override {
    should_scheme_match_upstream_ = should_match_upstream;
  }

  bool shouldDrainConnectionUponCompletion() const override { return should_drain_connection_; }

  void setShouldDrainConnectionUponCompletion(bool should_drain) override {
    should_drain_connection_ = should_drain;
  }

  void setParentStreamInfo(const StreamInfo& parent_stream_info) override {
    parent_stream_info_ = parent_stream_info;
  }

  OptRef<const StreamInfo> parentStreamInfo() const override { return parent_stream_info_; }

  void clearParentStreamInfo() override { parent_stream_info_.reset(); }

  TimeSource& time_source_;
  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;
  absl::optional<MonotonicTime> final_time_;
  absl::optional<Http::Protocol> protocol_;

private:
  absl::optional<uint32_t> response_code_;
  absl::optional<std::string> response_code_details_;
  absl::optional<std::string> connection_termination_details_;

public:
  absl::InlinedVector<ResponseFlag, 4> response_flags_{};
  bool health_check_request_{};
  Router::RouteConstSharedPtr route_;
  envoy::config::core::v3::Metadata metadata_{};
  FilterStateSharedPtr filter_state_;

private:
  absl::optional<uint32_t> attempt_count_;
  // TODO(agrawroh): Check if the owner of this storage outlives the StreamInfo. We should only copy
  // the string if it could outlive the StreamInfo.
  absl::optional<std::string> virtual_cluster_name_;

  static Network::ConnectionInfoProviderSharedPtr emptyDownstreamAddressProvider() {
    MUTABLE_CONSTRUCT_ON_FIRST_USE(
        Network::ConnectionInfoProviderSharedPtr,
        std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr));
  }

  std::shared_ptr<UpstreamInfo> upstream_info_;
  uint64_t bytes_received_{};
  uint64_t bytes_retransmitted_{};
  uint64_t packets_retransmitted_{};
  uint64_t bytes_sent_{};
  const Network::ConnectionInfoProviderSharedPtr downstream_connection_info_provider_;
  const Http::RequestHeaderMap* request_headers_{};
  StreamIdProviderSharedPtr stream_id_provider_;
  absl::optional<DownstreamTiming> downstream_timing_;
  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstream_cluster_info_;
  Tracing::Reason trace_reason_;
  // Default construct the object because upstream stream is not constructed in some cases.
  BytesMeterSharedPtr upstream_bytes_meter_{std::make_shared<BytesMeter>()};
  BytesMeterSharedPtr downstream_bytes_meter_;
  bool is_shadow_{false};
  std::string downstream_transport_failure_reason_;
  bool should_scheme_match_upstream_{false};
  bool should_drain_connection_{false};
  OptRef<const StreamInfo> parent_stream_info_;
};

} // namespace StreamInfo
} // namespace Envoy
