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
      : metadata_(config.metadata()), direction_(config.traffic_direction()),
        is_quic_(config.udp_listener_config().has_quic_options()),
        bypass_overload_manager_(config.bypass_overload_manager()) {}
  ListenerInfoImpl() = default;

  // Network::ListenerInfo
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override { return direction_; }
  bool shouldBypassOverloadManager() const override { return bypass_overload_manager_; };
  bool isQuic() const override { return is_quic_; }

private:
  const ListenerMetadataPack metadata_;
  const envoy::config::core::v3::TrafficDirection direction_{};
  const bool is_quic_{};
  const bool bypass_overload_manager_{};
};

} // namespace Server
} // namespace Envoy
