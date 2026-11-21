#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_socket_interface.h"

#include <chrono>

#include "envoy/extensions/network/socket_interface/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"
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

constexpr uint32_t ValidSocketFlags =
    envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo |
    envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;

envoy_dynamic_module_type_socket_interface_socket_type
toAbiSocketType(Network::Socket::Type socket_type) {
  return socket_type == Network::Socket::Type::Stream
             ? envoy_dynamic_module_type_socket_interface_socket_type_Stream
             : envoy_dynamic_module_type_socket_interface_socket_type_Datagram;
}

// Returns why a socket the module created cannot be used, or an empty string if it can.
std::string checkCreatedSocket(const DynamicModuleSocketInterfaceConfig& config, os_fd_t fd,
                               Network::Socket::Type socket_type, int domain,
                               bool already_connected, uint32_t flags,
                               envoy_dynamic_module_type_socket_interface_socket_module_ptr state) {
  const bool stream = socket_type == Network::Socket::Type::Stream;
  const bool delegate_io =
      flags & envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
  const bool hands_over =
      flags & envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
  if ((flags & ~ValidSocketFlags) != 0 || (delegate_io && hands_over)) {
    return "an invalid combination of flags";
  }
  if (delegate_io && (!stream || !config.implementsIoHooks())) {
    return "the DelegateIo flag on a socket that cannot use it";
  }
  if (hands_over && (!stream || already_connected || !config.implementsAccept())) {
    return "the HandsOverConnections flag on a socket that cannot use it";
  }
  if (flags != 0 && state == nullptr) {
    return "a flag without a per-socket state";
  }
  if (flags != 0 && !config.hotRestartDisabled()) {
    return "a flag while hot restart is enabled";
  }
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  sockaddr_storage own_addr = {};
  socklen_t own_addr_len = sizeof(own_addr);
  int type = 0;
  socklen_t type_len = sizeof(type);
  if (!SOCKET_VALID(fd) ||
      os_sys_calls.getsockname(fd, reinterpret_cast<sockaddr*>(&own_addr), &own_addr_len)
              .return_value_ != 0 ||
      own_addr.ss_family != domain ||
      os_sys_calls.getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len).return_value_ != 0 ||
      type != (stream ? SOCK_STREAM : SOCK_DGRAM)) {
    return "a file descriptor that is not a socket of the requested family and type";
  }
  return "";
}

} // namespace

// =============================================================================
// DynamicModuleSocketInterfaceExtension
// =============================================================================

DynamicModuleSocketInterfaceExtension::DynamicModuleSocketInterfaceExtension(
    DynamicModuleSocketInterface& sock_interface,
    DynamicModuleSocketInterfaceConfigSharedPtr config)
    : SocketInterfaceExtension(sock_interface), config_(std::move(config)) {
  // createBootstrapExtension() rejects a second extension, so only one owns the back pointer.
  ASSERT(static_cast<DynamicModuleSocketInterface&>(sock_interface_).extension_ == nullptr);
  static_cast<DynamicModuleSocketInterface&>(sock_interface_).extension_ = this;
}

DynamicModuleSocketInterfaceExtension::~DynamicModuleSocketInterfaceExtension() {
  auto& sock_interface = static_cast<DynamicModuleSocketInterface&>(sock_interface_);
  if (sock_interface.extension_ == this) {
    sock_interface.extension_ = nullptr;
  }
}

// =============================================================================
// DynamicModuleSocketInterface
// =============================================================================

bool DynamicModuleSocketInterface::ipFamilySupported(int domain) {
  if (extension_ == nullptr) {
    return Network::SocketInterfaceImpl::ipFamilySupported(domain);
  }
  // Envoy reads this as host support, so a family the module declines stays available through the
  // standard socket interface.
  const auto* config = extension_->config().get();
  return config->on_ip_family_supported_(config->in_module_config_, domain) ||
         Network::SocketInterfaceImpl::ipFamilySupported(domain);
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
    domain = AF_UNIX;
    // An abstract socket's name starts with a NUL byte, which asString() renders as "@".
    address = addr->asString();
    port = 0;
    socket_v6only = false;
    return;
  }
  PANIC("unexpected address type for a socket interface socket");
}

Network::IoHandlePtr DynamicModuleSocketInterface::createModuleSocket(
    Network::Socket::Type socket_type, absl::string_view address, uint32_t port, int domain,
    bool socket_v6only, const Network::SocketCreationOptions& options, bool& use_default) const {
  const auto& config = extension_->config();
  const envoy_dynamic_module_type_envoy_buffer address_buf = {.ptr = address.data(),
                                                              .length = address.size()};
  const envoy_dynamic_module_type_socket_interface_creation_options abi_options = {
      .mptcp_enabled = options.mptcp_enabled_,
      .max_addresses_cache_size = options.max_addresses_cache_size_};
  int64_t fd = INVALID_SOCKET;
  uint32_t flags = 0;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr = nullptr;
  const auto result =
      config->on_socket_(config->in_module_config_, toAbiSocketType(socket_type), address_buf, port,
                         domain, socket_v6only, &abi_options, &fd, &flags, &socket_module_ptr);
  use_default = false;
  switch (result) {
  case envoy_dynamic_module_type_socket_interface_creation_result_UseDefault:
    use_default = true;
    return nullptr;
  case envoy_dynamic_module_type_socket_interface_creation_result_Created:
  case envoy_dynamic_module_type_socket_interface_creation_result_AlreadyConnected:
    break;
  case envoy_dynamic_module_type_socket_interface_creation_result_Error:
  default:
    config->stats().socket_failed_.inc();
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(10),
                       "Dynamic module socket interface '{}' failed to create a socket for '{}:{}'",
                       config->socketInterfaceName(), address, port);
    return makeFailedIoHandle(domain, socket_type, socket_v6only);
  }

  const bool already_connected =
      result == envoy_dynamic_module_type_socket_interface_creation_result_AlreadyConnected;
  const os_fd_t socket_fd = static_cast<os_fd_t>(fd);
  const std::string rejection = checkCreatedSocket(*config, socket_fd, socket_type, domain,
                                                   already_connected, flags, socket_module_ptr);
  if (!rejection.empty()) {
    if (SOCKET_VALID(socket_fd)) {
      Api::OsSysCallsSingleton::get().close(socket_fd);
    }
    if (socket_module_ptr != nullptr) {
      config->on_socket_close_(config->in_module_config_, socket_module_ptr);
    }
    config->stats().socket_failed_.inc();
    ENVOY_LOG_PERIODIC(error, std::chrono::seconds(10),
                       "Dynamic module socket interface '{}' returned {} for '{}:{}'",
                       config->socketInterfaceName(), rejection, address, port);
    return makeFailedIoHandle(domain, socket_type, socket_v6only);
  }

  if (flags == 0 && socket_module_ptr == nullptr && !already_connected) {
    return Network::SocketInterfaceImpl::makeSocket(socket_fd, socket_v6only, socket_type, domain,
                                                    options);
  }
  const bool delegate_io =
      flags & envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
  auto handle = std::make_unique<DynamicModuleIoHandle>(
      config, socket_fd, socket_v6only, domain, options, socket_module_ptr, flags,
      already_connected, /*connecting=*/delegate_io);
  if (flags != 0) {
    handle->attachReadyHandle();
  }
  return handle;
}

Network::IoHandlePtr
DynamicModuleSocketInterface::socket(Network::Socket::Type socket_type,
                                     Network::Address::Type addr_type,
                                     Network::Address::IpVersion version, bool socket_v6only,
                                     const Network::SocketCreationOptions& options) const {
  if (extension_ == nullptr) {
    ENVOY_LOG_ONCE(error,
                   "{} is the default socket interface without its bootstrap extension, so "
                   "sockets are created by the standard socket interface",
                   name());
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
  bool use_default = false;
  auto handle = createModuleSocket(socket_type, "", 0, domain, socket_v6only, options, use_default);
  if (use_default) {
    return Network::SocketInterfaceImpl::socket(socket_type, addr_type, version, socket_v6only,
                                                options);
  }
  return handle;
}

Network::IoHandlePtr
DynamicModuleSocketInterface::socket(Network::Socket::Type socket_type,
                                     const Network::Address::InstanceConstSharedPtr addr,
                                     const Network::SocketCreationOptions& options) const {
  if (extension_ == nullptr) {
    ENVOY_LOG_ONCE(error,
                   "{} is the default socket interface without its bootstrap extension, so "
                   "sockets are created by the standard socket interface",
                   name());
    return Network::SocketInterfaceImpl::socket(socket_type, addr, options);
  }
  std::string address;
  uint32_t port = 0;
  int domain = 0;
  bool socket_v6only = false;
  addressToModuleArgs(addr, address, port, domain, socket_v6only);
  bool use_default = false;
  auto handle =
      createModuleSocket(socket_type, address, port, domain, socket_v6only, options, use_default);
  if (use_default) {
    return Network::SocketInterfaceImpl::socket(socket_type, addr, options);
  }
  return handle;
}

ProtobufTypes::MessagePtr DynamicModuleSocketInterface::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::network::socket_interface::dynamic_modules::v3::
                              DynamicModuleSocketInterface>();
}

Server::BootstrapExtensionCreationResult DynamicModuleSocketInterface::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {
#ifdef WIN32
  UNREFERENCED_PARAMETER(config);
  UNREFERENCED_PARAMETER(context);
  return absl::UnimplementedError(
      "The dynamic module socket interface is supported on POSIX platforms only");
#else
  if (extension_ != nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Only one ", name(), " bootstrap extension can be configured"));
  }
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::network::socket_interface::
                                           dynamic_modules::v3::DynamicModuleSocketInterface&>(
          config, context.messageValidationVisitor());

  // Socket interfaces are created before the cluster manager exists, so a remote module source
  // cannot be fetched.
  const auto& module_config = proto_config.dynamic_module_config();
  if (module_config.has_module() && module_config.module().has_remote()) {
    Extensions::DynamicModules::incrementLoadFailure(
        context, proto_config.socket_interface_name(),
        Extensions::DynamicModules::RemoteFetchErrorStat);
    return absl::InvalidArgumentError(
        "Dynamic module socket interface does not support remote module sources");
  }
  auto load_result =
      newDynamicModuleByConfig(module_config, proto_config.socket_interface_name(), context);
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

  auto module_config_or = newDynamicModuleSocketInterfaceConfig(
      proto_config.socket_interface_name(), socket_interface_config, std::move(load_result->loaded),
      context.serverScope());
  RETURN_IF_NOT_OK_REF(module_config_or.status());
  auto& socket_interface_module = module_config_or.value();
  socket_interface_module->setHotRestartDisabled(context.options().hotRestartDisabled());
  return Server::BootstrapExtensionPtr{std::make_unique<DynamicModuleSocketInterfaceExtension>(
      *this, std::move(socket_interface_module))};
#endif
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
