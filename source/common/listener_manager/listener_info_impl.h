#pragma once

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/listener.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Server {

using ListenerMetadataPack =
    Envoy::Config::MetadataPack<Envoy::Network::ListenerTypedMetadataFactory>;

class ListenerInfoImpl : public Network::ListenerInfo {
public:
  explicit ListenerInfoImpl(const envoy::config::listener::v3::Listener& config)
      : name_(config.name()), metadata_(config.metadata()), direction_(config.traffic_direction()),
        is_quic_(config.udp_listener_config().has_quic_options()),
        bypass_overload_manager_(config.bypass_overload_manager()),
        drain_type_(config.drain_type()) {}
  // For listeners that are not built from a Listener proto and only need to declare a drain type,
  // which is what decides whether their connections are drain-closed (see
  // Network::listenerDrainType() and Network::shouldDrainClose()). All other fields keep their
  // default-constructed values.
  explicit ListenerInfoImpl(envoy::config::listener::v3::Listener::DrainType drain_type)
      : drain_type_(drain_type) {}
  ListenerInfoImpl() = default;

  // Network::ListenerInfo
  absl::string_view name() const override { return name_; }
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override { return direction_; }
  bool shouldBypassOverloadManager() const override { return bypass_overload_manager_; };
  bool isQuic() const override { return is_quic_; }
  envoy::config::listener::v3::Listener::DrainType drainType() const override {
    return drain_type_;
  }

private:
  const std::string name_;
  const ListenerMetadataPack metadata_;
  const envoy::config::core::v3::TrafficDirection direction_{};
  const bool is_quic_{};
  const bool bypass_overload_manager_{};
  // Defaults to Listener::DEFAULT, which is the proto default (0).
  const envoy::config::listener::v3::Listener::DrainType drain_type_{};
};

} // namespace Server
} // namespace Envoy
