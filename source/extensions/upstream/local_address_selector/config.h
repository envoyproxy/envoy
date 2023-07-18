#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/upstream/local_address_selector/v3/default_local_address_selector.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

class DefaultUpstreamLocalAddressSelectorFactory : public UpstreamLocalAddressSelectorFactory {
public:
  std::string name() const override;

  UpstreamLocalAddressSelectorConstSharedPtr
  createLocalAddressSelector(::Envoy::OptRef<const envoy::config::core::v3::BindConfig> bind_config,
                             Network::ConnectionSocket::OptionsSharedPtr base_socket_options,
                             Network::ConnectionSocket::OptionsSharedPtr cluster_socket_options,
                             absl::optional<std::string> cluster_name) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstream::local_address_selector::v3::DefaultLocalAddressSelector>();
  }
};

DECLARE_FACTORY(DefaultUpstreamLocalAddressSelectorFactory);

} // namespace Upstream
} // namespace Envoy
