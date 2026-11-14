#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_socket_interface.h"

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/network/socket_interface/dynamic_modules/socket_interface_config.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

// The interface types live in ::Envoy::Network, which the enclosing namespace name shadows.
namespace Network = ::Envoy::Network;

namespace {

using ::Envoy::Extensions::DynamicModules::newDynamicModuleByConfig;

envoy_dynamic_module_type_socket_interface_socket_type
toAbiSocketType(Network::Socket::Type socket_type) {
  return socket_type == Network::Socket::Type::Stream
             ? envoy_dynamic_module_type_socket_interface_socket_type_Stream
             : envoy_dynamic_module_type_socket_interface_socket_type_Datagram;
}

} // namespace

// =============================================================================
// DynamicModuleSocketInterfaceExtension
// =============================================================================

DynamicModuleSocketInterfaceExtension::DynamicModuleSocketInterfaceExtension(
    DynamicModuleSocketInterface& sock_interface,
    DynamicModuleSocketInterfaceConfigSharedPtr config)
    : SocketInterfaceExtension(sock_interface), config_(std::move(config)) {
  // The interface is a process wide singleton, so only one extension may own its back pointer.
  ASSERT(static_cast<DynamicModuleSocketInterface&>(sock_interface_).extension_ == nullptr);
  static_cast<DynamicModuleSocketInterface&>(sock_interface_).extension_ = this;
}

DynamicModuleSocketInterfaceExtension::~DynamicModuleSocketInterfaceExtension() {
  // Clear the back pointer only if it still refers to this extension, so a second extension
  // that had already overwritten it is not nulled out from under the interface.
  auto& sock_interface = static_cast<DynamicModuleSocketInterface&>(sock_interface_);
  if (sock_interface.extension_ == this) {
    sock_interface.extension_ = nullptr;
  }
}

void DynamicModuleSocketInterfaceExtension::onServerInitialized(Server::Instance&) {
  if (config_->on_server_initialized_ != nullptr) {
    config_->on_server_initialized_(config_->in_module_config_);
  }
}

void DynamicModuleSocketInterfaceExtension::onWorkerThreadInitialized() {
  if (config_->on_worker_thread_initialized_ != nullptr) {
    config_->on_worker_thread_initialized_(config_->in_module_config_);
  }
}

// =============================================================================
// DynamicModuleSocketInterface
// =============================================================================

bool DynamicModuleSocketInterface::ipFamilySupported(int domain) {
  if (extension_ == nullptr) {
    return Network::SocketInterfaceImpl::ipFamilySupported(domain);
  }
  const auto* config = extension_->config().get();
  return config->on_ip_family_supported_(config->in_module_config_, domain);
}

void DynamicModuleSocketInterface::addressToModuleArgs(
    const Network::Address::InstanceConstSharedPtr& addr, std::string& address, uint32_t& port,
    int& domain, bool& socket_v6only) const {
  if (addr->type() == Network::Address::Type::Ip) {
    const auto& ip = *addr->ip();
    domain = ip.version() == Network::Address::IpVersion::v6 ? AF_INET6 : AF_INET;
    address = ip.addressAsString();
    port = ip.port();
    socket_v6only = ip.version() == Network::Address::IpVersion::v6 ? ip.ipv6()->v6only() : false;
    return;
  }
  if (addr->type() == Network::Address::Type::Pipe) {
    const auto& pipe = *static_cast<const Network::Address::PipeInstance*>(addr.get());
    domain = AF_UNIX;
    address = pipe.getSockAddr().sun_path;
    port = 0;
    socket_v6only = false;
    return;
  }
  PANIC("unexpected address type for a socket interface socket");
}

absl::StatusOr<Network::IoHandlePtr> DynamicModuleSocketInterface::createModuleSocket(
    Network::Socket::Type socket_type, absl::string_view address, uint32_t port, int domain,
    bool socket_v6only, const Network::SocketCreationOptions& options) const {
  const auto* config = extension_->config().get();
  const envoy_dynamic_module_type_envoy_buffer address_buf = {.ptr = address.data(),
                                                              .length = address.size()};
  const envoy_dynamic_module_type_socket_interface_creation_options abi_options = {
      .mptcp_enabled = options.mptcp_enabled_,
      .max_addresses_cache_size = options.max_addresses_cache_size_};
  int64_t fd = INVALID_SOCKET;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr = nullptr;
  const auto result =
      config->on_socket_(config->in_module_config_, toAbiSocketType(socket_type), address_buf, port,
                         domain, socket_v6only, &abi_options, &fd, &socket_module_ptr);
  if (result == envoy_dynamic_module_type_socket_interface_creation_result_UNSUPPORTED_FAMILY) {
    return absl::UnimplementedError(
        absl::StrCat(name(), " does not support the address family of ", address, ":", port));
  }
  const bool already_connected =
      result == envoy_dynamic_module_type_socket_interface_creation_result_ALREADY_CONNECTED;
  if (result != envoy_dynamic_module_type_socket_interface_creation_result_OK &&
      !already_connected) {
    IS_ENVOY_BUG(fmt::format("Dynamic module socket interface '{}' failed to create a socket "
                             "for '{}:{}' (result {})",
                             name(), address, port, static_cast<int>(result)));
    return absl::InternalError(
        absl::StrCat(name(), " failed to create a socket for ", address, ":", port));
  }
  // A module without I/O hooks may keep no per-socket state, so the module pointer is only
  // required when the delegating handle will pass it to the I/O hooks.
  if (!SOCKET_VALID(static_cast<os_fd_t>(fd)) ||
      (config->implementsIoHooks() && socket_module_ptr == nullptr)) {
    IS_ENVOY_BUG(fmt::format("Dynamic module socket interface '{}' returned an invalid socket "
                             "for '{}:{}'",
                             name(), address, port));
    if (SOCKET_VALID(static_cast<os_fd_t>(fd))) {
      Api::OsSysCallsSingleton::get().close(fd);
    }
    return absl::InternalError(
        absl::StrCat(name(), " returned an invalid socket for ", address, ":", port));
  }

  if (config->implementsIoHooks()) {
    auto handle = std::make_unique<DynamicModuleIoHandle>(
        extension_->config(), static_cast<os_fd_t>(fd), socket_v6only, domain, options,
        socket_module_ptr, already_connected);
    auto* ready_handle = new SocketReadyHandle(handle->sharedState().weak_from_this());
    config->on_set_socket_handle_(socket_module_ptr, ready_handle);
    return handle;
  }
  if (already_connected || socket_module_ptr != nullptr) {
    // A standard handle reads and writes the socket itself: it cannot skip connect() on an
    // already-connected socket, and it never calls socket_close, so per-socket module state
    // would leak.
    IS_ENVOY_BUG(
        fmt::format("Dynamic module socket interface '{}' returned a socket for '{}:{}' that "
                    "requires the I/O hooks without implementing them",
                    name(), address, port));
    if (socket_module_ptr != nullptr) {
      config->on_socket_close_(config->in_module_config_, socket_module_ptr);
    }
    Api::OsSysCallsSingleton::get().close(fd);
    return absl::InternalError(
        absl::StrCat(name(), " returned a socket for ", address, ":", port,
                     " that requires the I/O hooks without implementing them"));
  }
  return Network::SocketInterfaceImpl::makeSocket(static_cast<os_fd_t>(fd), socket_v6only,
                                                  socket_type, domain, options);
}

Network::IoHandlePtr
DynamicModuleSocketInterface::socket(Network::Socket::Type socket_type,
                                     Network::Address::Type addr_type,
                                     Network::Address::IpVersion version, bool socket_v6only,
                                     const Network::SocketCreationOptions& options) const {
  if (extension_ == nullptr) {
    return Network::SocketInterfaceImpl::socket(socket_type, addr_type, version, socket_v6only,
                                                options);
  }
  int domain;
  if (addr_type == Network::Address::Type::Ip) {
    domain = version == Network::Address::IpVersion::v6 || Network::Address::forceV6() ? AF_INET6
                                                                                       : AF_INET;
  } else if (addr_type == Network::Address::Type::Pipe) {
    domain = AF_UNIX;
  } else {
    PANIC("not implemented");
  }
  auto handle_or = createModuleSocket(socket_type, "", 0, domain, socket_v6only, options);
  if (!handle_or.ok()) {
    if (absl::IsUnimplemented(handle_or.status())) {
      // The module does not handle this address family; use the standard socket interface.
      return Network::SocketInterfaceImpl::socket(socket_type, addr_type, version, socket_v6only,
                                                  options);
    }
    return nullptr;
  }
  return std::move(handle_or.value());
}

Network::IoHandlePtr
DynamicModuleSocketInterface::socket(Network::Socket::Type socket_type,
                                     const Network::Address::InstanceConstSharedPtr addr,
                                     const Network::SocketCreationOptions& options) const {
  if (extension_ == nullptr) {
    return Network::SocketInterfaceImpl::socket(socket_type, addr, options);
  }
  std::string address;
  uint32_t port = 0;
  int domain = 0;
  bool socket_v6only = false;
  addressToModuleArgs(addr, address, port, domain, socket_v6only);
  auto handle_or = createModuleSocket(socket_type, address, port, domain, socket_v6only, options);
  if (!handle_or.ok()) {
    if (absl::IsUnimplemented(handle_or.status())) {
      // The module does not handle this address family; use the standard socket interface.
      return Network::SocketInterfaceImpl::socket(socket_type, addr, options);
    }
    return nullptr;
  }
  return std::move(handle_or.value());
}

ProtobufTypes::MessagePtr DynamicModuleSocketInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::network::socket_interface::dynamic_modules::v3::
                              DynamicModuleSocketInterface>();
}

Server::BootstrapExtensionCreationResult DynamicModuleSocketInterface::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::network::socket_interface::
                                           dynamic_modules::v3::DynamicModuleSocketInterface&>(
          config, context.messageValidationVisitor());

  // Socket interfaces are created before the cluster manager exists, so only the synchronous
  // local-file and by-name module sources can succeed here. A remote source is rejected by the
  // loader.
  auto load_result = newDynamicModuleByConfig(proto_config.dynamic_module_config(),
                                              proto_config.socket_interface_name(), context);
  if (!load_result.ok()) {
    return load_result.status();
  }

  std::string socket_interface_config;
  if (proto_config.has_socket_interface_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.socket_interface_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("Failed to parse socket interface config: ",
                                                     config_or_error.status().message()));
    }
    socket_interface_config = std::move(config_or_error.value());
  }

  auto module_config = newDynamicModuleSocketInterfaceConfig(proto_config.socket_interface_name(),
                                                             socket_interface_config,
                                                             std::move(load_result->loaded));
  RETURN_IF_NOT_OK_REF(module_config.status());
  return Server::BootstrapExtensionPtr{std::make_unique<DynamicModuleSocketInterfaceExtension>(
      *this, std::move(module_config.value()))};
}

/**
 * Static registration for the dynamic module socket interface factory. @see RegistryFactory.
 */
REGISTER_FACTORY(DynamicModuleSocketInterface, Server::Configuration::BootstrapExtensionFactory);

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
