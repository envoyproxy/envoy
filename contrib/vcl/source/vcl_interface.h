#pragma once

#include "envoy/network/socket.h"

#include "source/common/network/socket_interface.h"

#include "vppcom.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace Vcl {

#define VCL_DEBUG (0)
#define VCL_RX_ZC (0)

#if VCL_DEBUG > 0
#define VCL_LOG(fmt, _args...) fprintf(stderr, "[%d] " fmt "\n", vppcom_worker_index(), ##_args)
#else
#define VCL_LOG(fmt, _args...)
#endif

void vcl_interface_worker_register();
uint32_t& vcl_epoll_handle(uint32_t wrk_index);
void vcl_interface_register_epoll_event(Envoy::Event::Dispatcher& dispatcher);
void vclSocketInterfaceInit(Event::Dispatcher& dispatcher);

class VclSocketInterface : public Envoy::Network::SocketInterfaceBase {
public:
  // SocketInterface
  Envoy::Network::IoHandlePtr socket(Envoy::Network::Socket::Type socket_type,
                                     Envoy::Network::Address::Type addr_type,
                                     Envoy::Network::Address::IpVersion version,
                                     bool socket_v6only) const override;
  Envoy::Network::IoHandlePtr
  socket(Envoy::Network::Socket::Type socket_type,
         const Envoy::Network::Address::InstanceConstSharedPtr addr) const override;
  bool ipFamilySupported(int domain) override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override {
    return "envoy.extensions.network.socket_interface.vcl.vcl_socket_interface";
  };

private:
  absl::flat_hash_map<int, Envoy::Event::FileEventPtr> mq_events_;
};

} // namespace Vcl
} // namespace Network
} // namespace Extensions
} // namespace Envoy