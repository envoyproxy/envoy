#include <cassert>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk_network.h"

namespace Envoy {
namespace DynamicModules {

class FlowControlFilter : public NetworkFilter {
public:
  explicit FlowControlFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override { return NetworkFilterStatus::Continue; }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    if (!reads_disabled_) {
      const auto disable_status = handle_.readDisable(true);
      assert(disable_status == NetworkReadDisableStatus::TransitionedToReadDisabled);
      assert(!handle_.readEnabled());
      reads_disabled_ = true;

      const auto enable_status = handle_.readDisable(false);
      assert(enable_status == NetworkReadDisableStatus::TransitionedToReadEnabled);
      assert(handle_.readEnabled());
    }
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
  bool reads_disabled_{false};
};

class FlowControlFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<FlowControlFilter>(handle);
  }
};

class FlowControlConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<FlowControlFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(FlowControlConfigFactory, "flow_control");

class ConnectionStateFilter : public NetworkFilter {
public:
  explicit ConnectionStateFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    assert(handle_.getConnectionState() == NetworkConnectionState::Open);
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
};

class ConnectionStateFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<ConnectionStateFilter>(handle);
  }
};

class ConnectionStateConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<ConnectionStateFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(ConnectionStateConfigFactory, "connection_state");

class HalfCloseFilter : public NetworkFilter {
public:
  explicit HalfCloseFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    assert(handle_.isHalfCloseEnabled());
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    handle_.enableHalfClose(false);
    assert(!handle_.isHalfCloseEnabled());
    handle_.enableHalfClose(true);
    assert(handle_.isHalfCloseEnabled());
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

private:
  NetworkFilterHandle& handle_;
};

class HalfCloseFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<HalfCloseFilter>(handle);
  }
};

class HalfCloseConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<HalfCloseFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(HalfCloseConfigFactory, "half_close");

class BufferLimitsFilter : public NetworkFilter {
public:
  explicit BufferLimitsFilter(NetworkFilterHandle& handle) : handle_(handle) {}

  NetworkFilterStatus onNewConnection() override {
    initial_buffer_limit_ = handle_.getBufferLimit();
    handle_.setBufferLimits(32768);
    assert(handle_.getBufferLimit() == 32768);
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onRead(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  NetworkFilterStatus onWrite(NetworkBuffer&, bool) override {
    return NetworkFilterStatus::Continue;
  }

  void onEvent(NetworkConnectionEvent) override {}
  void onDestroy() override {}

  void onAboveWriteBufferHighWatermark() override { above_high_watermark_called_ = true; }
  void onBelowWriteBufferLowWatermark() override { below_low_watermark_called_ = true; }

private:
  NetworkFilterHandle& handle_;
  inline static bool above_high_watermark_called_{false};
  inline static bool below_low_watermark_called_{false};
  inline static uint32_t initial_buffer_limit_{0};
};

class BufferLimitsFactory : public NetworkFilterFactory {
public:
  std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) override {
    return std::make_unique<BufferLimitsFilter>(handle);
  }
};

class BufferLimitsConfigFactory : public NetworkFilterConfigFactory {
public:
  std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle&,
                                               std::string_view) override {
    return std::make_unique<BufferLimitsFactory>();
  }
};

REGISTER_NETWORK_FILTER_CONFIG_FACTORY(BufferLimitsConfigFactory, "buffer_limits");

} // namespace DynamicModules
} // namespace Envoy
