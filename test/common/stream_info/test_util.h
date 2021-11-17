#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/common/random_generator.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/request_id/uuid/config.h"

#include "test/mocks/common.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {

class TestStreamInfo : public StreamInfo::StreamInfoImpl {
public:
  TestStreamInfo(TimeSource& time_source) : StreamInfoImpl(time_source, nullptr) {
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

  const Network::ConnectionInfoSetter& downstreamAddressProvider() const override {
    return *downstream_connection_info_provider_;
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

  void setVirtualClusterName(const std::string& virtual_cluster_name) override {
    virtual_cluster_name_ = virtual_cluster_name;
  }
  const std::string& getVirtualClusterName() const override { return virtual_cluster_name_; }

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
    return duration(downstream_timing_.firstDownstreamTxByteSent());
  }

  absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent() const override {
    return duration(downstream_timing_.lastDownstreamTxByteSent());
  }

  void onRequestComplete() override { end_time_ = timeSystem().monotonicTime(); }

  absl::optional<std::chrono::nanoseconds> requestComplete() const override {
    return duration(end_time_);
  }

  void setRequestIDProvider(const Http::RequestIdStreamInfoProviderSharedPtr& provider) override {
    ASSERT(provider != nullptr);
    request_id_provider_ = provider;
  }
  const Http::RequestIdStreamInfoProvider* getRequestIDProvider() const override {
    return request_id_provider_.get();
  }

  Event::TimeSystem& timeSystem() { return test_time_.timeSystem(); }

  Random::RandomGeneratorImpl random_;
  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;
  absl::optional<MonotonicTime> end_time_;
  absl::optional<Http::Protocol> protocol_{Http::Protocol::Http11};
  absl::optional<uint32_t> response_code_;
  absl::optional<std::string> response_code_details_;
  absl::optional<std::string> connection_termination_details_;
  uint64_t response_flags_{};
  Upstream::HostDescriptionConstSharedPtr upstream_host_{};
  bool health_check_request_{};
  std::string route_name_;
  std::string virtual_cluster_name_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::ConnectionInfoSetterSharedPtr downstream_connection_info_provider_{
      std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr)};
  Envoy::Event::SimulatedTimeSystem test_time_;
  Http::RequestIdStreamInfoProviderSharedPtr request_id_provider_;
};

} // namespace Envoy
