#include "test/mocks/stream_info/mocks.h"

#include "source/common/network/address_impl.h"

#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Invoke;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace StreamInfo {

MockUpstreamInfo::MockUpstreamInfo()
    : upstream_local_address_(new Network::Address::Ipv4Instance("127.1.2.3", 58443)),
      upstream_remote_address_(new Network::Address::Ipv4Instance("10.0.0.1", 443)),
      upstream_host_(new testing::NiceMock<Upstream::MockHostDescription>()) {
  ON_CALL(*this, dumpState(_, _)).WillByDefault(Invoke([](std::ostream& os, int indent_level) {
    os << "MockUpstreamInfo test dumpState with indent: " << indent_level << std::endl;
  }));
  ON_CALL(*this, setUpstreamConnectionId(_)).WillByDefault(Invoke([this](uint64_t id) {
    upstream_connection_id_ = id;
  }));
  ON_CALL(*this, upstreamConnectionId()).WillByDefault(ReturnPointee(&upstream_connection_id_));
  ON_CALL(*this, setUpstreamInterfaceName(_))
      .WillByDefault(
          Invoke([this](absl::string_view interface_name) { interface_name_ = interface_name; }));
  ON_CALL(*this, upstreamInterfaceName()).WillByDefault(ReturnPointee(&interface_name_));
  ON_CALL(*this, setUpstreamSslConnection(_))
      .WillByDefault(Invoke([this](const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info) {
        ssl_connection_info_ = ssl_connection_info;
      }));
  ON_CALL(*this, upstreamSslConnection()).WillByDefault(ReturnPointee(&ssl_connection_info_));
  ON_CALL(*this, upstreamTiming()).WillByDefault(ReturnRef(upstream_timing_));
  ON_CALL(Const(*this), upstreamTiming()).WillByDefault(ReturnRef(upstream_timing_));
  ON_CALL(*this, setUpstreamLocalAddress(_))
      .WillByDefault(
          Invoke([this](const Network::Address::InstanceConstSharedPtr& upstream_local_address) {
            upstream_local_address_ = upstream_local_address;
          }));
  ON_CALL(*this, upstreamLocalAddress()).WillByDefault(ReturnRef(upstream_local_address_));
  ON_CALL(*this, setUpstreamTransportFailureReason(_))
      .WillByDefault(Invoke([this](absl::string_view failure_reason) {
        failure_reason_ = std::string(failure_reason);
      }));
  ON_CALL(*this, upstreamTransportFailureReason()).WillByDefault(ReturnRef(failure_reason_));
  ON_CALL(*this, setUpstreamHost(_))
      .WillByDefault(Invoke([this](Upstream::HostDescriptionConstSharedPtr upstream_host) {
        upstream_host_ = upstream_host;
      }));
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&upstream_host_));
  ON_CALL(*this, setUpstreamFilterState(_))
      .WillByDefault(Invoke(
          [this](const FilterStateSharedPtr& filter_state) { filter_state_ = filter_state; }));
  ON_CALL(*this, upstreamFilterState()).WillByDefault(ReturnRef(filter_state_));
  ON_CALL(*this, setUpstreamNumStreams(_)).WillByDefault(Invoke([this](uint64_t num_streams) {
    num_streams_ = num_streams;
  }));
  ON_CALL(*this, upstreamNumStreams()).WillByDefault(ReturnPointee(&num_streams_));
  ON_CALL(*this, setUpstreamProtocol(_)).WillByDefault(Invoke([this](Http::Protocol protocol) {
    upstream_protocol_ = protocol;
  }));
  ON_CALL(*this, upstreamProtocol()).WillByDefault(ReturnPointee(&upstream_protocol_));
  ON_CALL(*this, upstreamRemoteAddress()).WillByDefault(ReturnRef(upstream_remote_address_));
}

MockUpstreamInfo::~MockUpstreamInfo() = default;

MockStreamInfo::MockStreamInfo()
    : start_time_(ts_.systemTime()),
      // upstream
      upstream_info_(std::make_shared<testing::NiceMock<MockUpstreamInfo>>()),
      filter_state_(std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain)),
      downstream_connection_info_provider_(std::make_shared<Network::ConnectionInfoSetterImpl>(
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2"),
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"))) {
  // downstream:direct_remote
  auto downstream_direct_remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.3", 63443)};
  downstream_connection_info_provider_->setDirectRemoteAddressForTest(
      downstream_direct_remote_address);

  ON_CALL(*this, setResponseFlag(_)).WillByDefault(Invoke([this](ResponseFlag response_flag) {
    response_flags_ |= response_flag;
  }));
  ON_CALL(*this, setResponseCode(_)).WillByDefault(Invoke([this](uint32_t code) {
    response_code_ = code;
  }));
  ON_CALL(*this, setResponseCodeDetails(_)).WillByDefault(Invoke([this](absl::string_view details) {
    response_code_details_ = std::string(details);
  }));
  ON_CALL(*this, setConnectionTerminationDetails(_))
      .WillByDefault(Invoke([this](absl::string_view details) {
        connection_termination_details_ = std::string(details);
      }));
  ON_CALL(*this, startTime()).WillByDefault(ReturnPointee(&start_time_));
  ON_CALL(*this, startTimeMonotonic()).WillByDefault(ReturnPointee(&start_time_monotonic_));
  ON_CALL(*this, currentDuration()).WillByDefault(ReturnPointee(&end_time_));
  ON_CALL(*this, requestComplete()).WillByDefault(ReturnPointee(&end_time_));
  ON_CALL(*this, onRequestComplete()).WillByDefault(Invoke([this]() {
    end_time_ = absl::make_optional<std::chrono::nanoseconds>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(ts_.systemTime() - start_time_)
            .count());
  }));
  ON_CALL(*this, downstreamTiming()).WillByDefault(Invoke([this]() -> DownstreamTiming& {
    return downstream_timing_;
  }));
  ON_CALL(Const(*this), downstreamTiming())
      .WillByDefault(
          Invoke([this]() -> OptRef<const DownstreamTiming> { return downstream_timing_; }));
  ON_CALL(*this, upstreamInfo()).WillByDefault(Invoke([this]() { return upstream_info_; }));
  ON_CALL(testing::Const(*this), upstreamInfo())
      .WillByDefault(Invoke([this]() -> OptRef<const UpstreamInfo> {
        if (!upstream_info_) {
          return {};
        }
        return *upstream_info_;
      }));
  ON_CALL(*this, downstreamAddressProvider())
      .WillByDefault(ReturnPointee(downstream_connection_info_provider_));
  ON_CALL(*this, protocol()).WillByDefault(ReturnPointee(&protocol_));
  ON_CALL(*this, responseCode()).WillByDefault(ReturnPointee(&response_code_));
  ON_CALL(*this, responseCodeDetails()).WillByDefault(ReturnPointee(&response_code_details_));
  ON_CALL(*this, connectionTerminationDetails())
      .WillByDefault(ReturnPointee(&connection_termination_details_));
  ON_CALL(*this, addBytesReceived(_)).WillByDefault(Invoke([this](uint64_t bytes_received) {
    bytes_received_ += bytes_received;
  }));
  ON_CALL(*this, bytesReceived()).WillByDefault(ReturnPointee(&bytes_received_));
  ON_CALL(*this, addBytesSent(_)).WillByDefault(Invoke([this](uint64_t bytes_sent) {
    bytes_sent_ += bytes_sent;
  }));
  ON_CALL(*this, bytesSent()).WillByDefault(ReturnPointee(&bytes_sent_));
  ON_CALL(*this, hasResponseFlag(_)).WillByDefault(Invoke([this](ResponseFlag flag) {
    return response_flags_ & flag;
  }));
  ON_CALL(*this, intersectResponseFlags(_)).WillByDefault(Invoke([this](uint64_t response_flags) {
    return (response_flags_ & response_flags) != 0;
  }));
  ON_CALL(*this, hasAnyResponseFlag()).WillByDefault(Invoke([this]() {
    return response_flags_ != 0;
  }));
  ON_CALL(*this, responseFlags()).WillByDefault(Invoke([this]() -> uint64_t {
    return response_flags_;
  }));
  ON_CALL(*this, dynamicMetadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(Const(*this), dynamicMetadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, filterState()).WillByDefault(ReturnRef(filter_state_));
  ON_CALL(Const(*this), filterState()).WillByDefault(Invoke([this]() -> const FilterState& {
    return *filter_state_;
  }));
  ON_CALL(*this, setVirtualClusterName(_))
      .WillByDefault(Invoke([this](const absl::optional<std::string>& virtual_cluster_name) {
        virtual_cluster_name_ = virtual_cluster_name;
      }));
  ON_CALL(*this, getRouteName()).WillByDefault(ReturnRef(route_name_));
  ON_CALL(*this, setUpstreamInfo(_))
      .WillByDefault(Invoke([this](std::shared_ptr<UpstreamInfo> info) { upstream_info_ = info; }));
  ON_CALL(*this, virtualClusterName()).WillByDefault(ReturnRef(virtual_cluster_name_));
  ON_CALL(*this, setAttemptCount(_)).WillByDefault(Invoke([this](uint32_t attempt_count) {
    attempt_count_ = attempt_count;
  }));
  ON_CALL(*this, attemptCount()).WillByDefault(Invoke([this]() { return attempt_count_; }));
  ON_CALL(*this, getUpstreamBytesMeter()).WillByDefault(ReturnPointee(&upstream_bytes_meter_));
  ON_CALL(*this, getDownstreamBytesMeter()).WillByDefault(ReturnPointee(&downstream_bytes_meter_));
  ON_CALL(*this, setUpstreamBytesMeter(_))
      .WillByDefault(Invoke([this](const BytesMeterSharedPtr& upstream_bytes_meter) {
        upstream_bytes_meter_ = upstream_bytes_meter;
      }));
  ON_CALL(*this, setDownstreamBytesMeter(_))
      .WillByDefault(Invoke([this](const BytesMeterSharedPtr& downstream_bytes_meter) {
        downstream_bytes_meter_ = downstream_bytes_meter;
      }));
  ON_CALL(*this, setDownstreamTransportFailureReason(_))
      .WillByDefault(Invoke([this](absl::string_view failure_reason) {
        downstream_transport_failure_reason_ = std::string(failure_reason);
      }));
  ON_CALL(*this, downstreamTransportFailureReason())
      .WillByDefault(ReturnPointee(&downstream_transport_failure_reason_));
  ON_CALL(*this, setUpstreamClusterInfo(_))
      .WillByDefault(Invoke([this](const Upstream::ClusterInfoConstSharedPtr& cluster_info) {
        upstream_cluster_info_ = std::move(cluster_info);
      }));
  ON_CALL(*this, upstreamClusterInfo()).WillByDefault(ReturnPointee(&upstream_cluster_info_));
}

MockStreamInfo::~MockStreamInfo() = default;

} // namespace StreamInfo
} // namespace Envoy
