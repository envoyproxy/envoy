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
    setUpstreamInfo(std::make_shared<Envoy::StreamInfo::UpstreamInfoImpl>());
  }

  SystemTime startTime() const override { return start_time_; }
  MonotonicTime startTimeMonotonic() const override { return start_time_monotonic_; }

  const Network::ConnectionInfoSetter& downstreamAddressProvider() const override {
    return *downstream_connection_info_provider_;
  }

  const absl::optional<std::string>& virtualClusterName() const override {
    return virtual_cluster_name_;
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
  absl::optional<std::string> virtual_cluster_name_;
  Network::ConnectionInfoSetterSharedPtr downstream_connection_info_provider_{
      std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr)};
  Envoy::Event::SimulatedTimeSystem test_time_;
  Http::RequestIdStreamInfoProviderSharedPtr request_id_provider_;
};

} // namespace Envoy
