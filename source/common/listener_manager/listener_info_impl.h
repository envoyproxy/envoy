#pragma once

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/listener.h"

#include "source/common/config/metadata.h"

namespace Envoy {
namespace Server {

class ListenerInfoImpl : public Network::ListenerInfo {
public:
  explicit ListenerInfoImpl(const envoy::config::listener::v3::Listener& config)
      : config_(config), typed_metadata_(config_.metadata()),
        is_quic_(config.udp_listener_config().has_quic_options()) {}
  ListenerInfoImpl() : typed_metadata_(config_.metadata()), is_quic_(false) {}

  // Allow access to the underlying protobuf as an internal detail.
  const envoy::config::listener::v3::Listener& config() const { return config_; }
  // Network::ListenerInfo
  const envoy::config::core::v3::Metadata& metadata() const override;
  const Envoy::Config::TypedMetadata& typedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override {
    return config_.traffic_direction();
  }
  bool isQuic() const override { return is_quic_; }

private:
  const envoy::config::listener::v3::Listener config_;
  const Envoy::Config::TypedMetadataImpl<Envoy::Network::ListenerTypedMetadataFactory>
      typed_metadata_;
  const bool is_quic_;
};

} // namespace Server
} // namespace Envoy
