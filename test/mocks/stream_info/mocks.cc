#include "test/mocks/stream_info/mocks.h"

#include "source/common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace StreamInfo {

MockStreamInfo::MockStreamInfo()
    : start_time_(ts_.systemTime()),
      filter_state_(std::make_shared<FilterStateImpl>(FilterState::LifeSpan::FilterChain)),
      downstream_connection_info_provider_(std::make_shared<Network::ConnectionInfoSetterImpl>(
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2"),
          std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"))) {
  // downstream:direct_remote
  auto downstream_direct_remote_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.3", 63443)};
  downstream_connection_info_provider_->setDirectRemoteAddressForTest(
      downstream_direct_remote_address);
  // upstream
  upstream_info_ = std::make_unique<UpstreamInfoImpl>();
  // upstream:host
  Upstream::HostDescriptionConstSharedPtr host{
      new testing::NiceMock<Upstream::MockHostDescription>()};
  upstream_info_->setUpstreamHost(host);
  // upstream:local
  auto upstream_local_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.1.2.3", 58443)};
  upstream_info_->setUpstreamLocalAddress(upstream_local_address);

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
  ON_CALL(testing::Const(*this), upstreamInfo()).WillByDefault(Invoke([this]() {
    return OptRef<const UpstreamInfo>(*upstream_info_);
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
  ON_CALL(*this, responseFlags()).WillByDefault(Return(response_flags_));
  ON_CALL(*this, dynamicMetadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(Const(*this), dynamicMetadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, filterState()).WillByDefault(ReturnRef(filter_state_));
  ON_CALL(Const(*this), filterState()).WillByDefault(Invoke([this]() -> const FilterState& {
    return *filter_state_;
  }));
  ON_CALL(*this, setRouteName(_)).WillByDefault(Invoke([this](const absl::string_view route_name) {
    route_name_ = std::string(route_name);
  }));
  ON_CALL(*this, setVirtualClusterName(_))
      .WillByDefault(Invoke([this](const absl::optional<std::string>& virtual_cluster_name) {
        virtual_cluster_name_ = virtual_cluster_name;
      }));
  ON_CALL(*this, getRouteName()).WillByDefault(ReturnRef(route_name_));
  ON_CALL(*this, setUpstreamInfo(_))
      .WillByDefault(Invoke([this](std::shared_ptr<UpstreamInfo> info) { upstream_info_ = info; }));
  ON_CALL(*this, virtualClusterName()).WillByDefault(ReturnRef(virtual_cluster_name_));
  ON_CALL(*this, setFilterChainName(_))
      .WillByDefault(Invoke([this](const absl::string_view filter_chain_name) {
        filter_chain_name_ = std::string(filter_chain_name);
      }));
  ON_CALL(*this, filterChainName()).WillByDefault(ReturnRef(filter_chain_name_));
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
}

MockStreamInfo::~MockStreamInfo() = default;

} // namespace StreamInfo
} // namespace Envoy
