#include "source/extensions/network/socket_interface/sockmap/socket_interface.h"

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/extensions/network/socket_interface/sockmap/v3/sockmap.pb.h"
#include "envoy/extensions/network/socket_interface/sockmap/v3/sockmap.pb.validate.h"

#include "source/common/common/fmt.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/network/socket_interface/sockmap/io_handle.h"

namespace Envoy {
namespace Network {

IoHandlePtr SockmapSocketInterface::makeSocket(int socket_fd, bool socket_v6only,
                                               Socket::Type socket_type, std::optional<int> domain,
                                               const SocketCreationOptions& options) const {
  // Only IPv4 stream sockets are accelerated, matching the eBPF datapath. Other sockets, including
  // IPv6 and AF_UNIX, use the standard datapath unchanged. The datapath lives on the bootstrap
  // extension, so an interface that was never configured has no extension and falls through.
  if (socket_type == Socket::Type::Stream && domain == AF_INET && extension_ != nullptr &&
      extension_->registerUserSpaceSockets() && extension_->datapath() != nullptr) {
    return std::make_unique<SockmapIoSocketHandle>(extension_->datapath(), socket_fd, socket_v6only,
                                                   domain, options.max_addresses_cache_size_);
  }
  return SocketInterfaceImpl::makeSocket(socket_fd, socket_v6only, socket_type, domain, options);
}

Server::BootstrapExtensionPtr SockmapSocketInterface::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::network::socket_interface::sockmap::v3::Sockmap&>(
      config, context.messageValidationVisitor());

  BpfDatapathConfig datapath_config;
  datapath_config.bpf_program_path = message.bpf_program_path();
  datapath_config.cgroup_path = message.cgroup_path();
  datapath_config.sockhash_max_entries =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(message, sockhash_max_entries, 65536);
  datapath_config.accelerated_ports.reserve(message.accelerated_ports().size());
  for (const auto& range : message.accelerated_ports()) {
    if (range.start() < 1 || range.end() > 65536 || range.start() >= range.end()) {
      throwEnvoyExceptionOrPanic(
          fmt::format("sockmap accelerated_ports range [{}, {}) is invalid, expected "
                      "1 <= start < end <= 65536",
                      range.start(), range.end()));
    }
    datapath_config.accelerated_ports.push_back(
        {static_cast<uint32_t>(range.start()), static_cast<uint32_t>(range.end())});
  }
  const bool register_user_space_sockets =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(message, register_user_space_sockets, true);
  BpfDatapathSharedPtr datapath = createDatapath(datapath_config);

  return std::make_unique<SockmapSocketInterfaceExtension>(*this, std::move(datapath),
                                                           register_user_space_sockets);
}

BpfDatapathSharedPtr SockmapSocketInterface::createDatapath(const BpfDatapathConfig& config) {
  return createBpfDatapath(config);
}

ProtobufTypes::MessagePtr SockmapSocketInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::network::socket_interface::sockmap::v3::Sockmap>();
}

REGISTER_FACTORY(SockmapSocketInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace Network
} // namespace Envoy
