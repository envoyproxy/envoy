#include "source/common/network/address_impl.h"

#include <array>
#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/network/socket_interface.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Network {
namespace Address {

namespace {

// Constructs a readable string with the embedded nulls in the abstract path replaced with '@'.
std::string friendlyNameFromAbstractPath(absl::string_view path) {
  std::string friendly_name(path.data(), path.size());
  std::replace(friendly_name.begin(), friendly_name.end(), '\0', '@');
  return friendly_name;
}

const SocketInterface* sockInterfaceOrDefault(const SocketInterface* sock_interface) {
  return sock_interface == nullptr ? &SocketInterfaceSingleton::get() : sock_interface;
}

void throwOnError(absl::Status status) {
  if (!status.ok()) {
    throwEnvoyExceptionOrPanic(status.ToString());
  }
}

InstanceConstSharedPtr throwOnError(StatusOr<InstanceConstSharedPtr> address) {
  if (!address.ok()) {
    throwOnError(address.status());
  }
  return *address;
}

} // namespace

bool forceV6() {
#if defined(__APPLE__) || defined(__ANDROID_API__)
  return Runtime::runtimeFeatureEnabled("envoy.reloadable_features.always_use_v6");
#else
  return false;
#endif
}

void ipv6ToIpv4CompatibleAddress(const struct sockaddr_in6* sin6, struct sockaddr_in* sin) {
#if defined(__APPLE__)
  *sin = {{}, AF_INET, sin6->sin6_port, {sin6->sin6_addr.__u6_addr.__u6_addr32[3]}, {}};
#elif defined(WIN32)
  struct in_addr in_v4 = {};
  in_v4.S_un.S_addr = reinterpret_cast<const uint32_t*>(sin6->sin6_addr.u.Byte)[3];
  *sin = {AF_INET, sin6->sin6_port, in_v4, {}};
#else
  *sin = {AF_INET, sin6->sin6_port, {sin6->sin6_addr.s6_addr32[3]}, {}};
#endif
}

StatusOr<Address::InstanceConstSharedPtr> addressFromSockAddr(const sockaddr_storage& ss,
                                                              socklen_t ss_len, bool v6only) {
  RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) >= sizeof(sa_family_t), "");
  if (forceV6()) {
    v6only = false;
  }
  switch (ss.ss_family) {
  case AF_INET: {
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) == sizeof(sockaddr_in), "");
    const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&ss);
    ASSERT(AF_INET == sin->sin_family);
    return Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(sin);
  }
  case AF_INET6: {
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) == sizeof(sockaddr_in6), "");
    const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&ss);
    ASSERT(AF_INET6 == sin6->sin6_family);
    if (!v6only && IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
      struct sockaddr_in sin;
      ipv6ToIpv4CompatibleAddress(sin6, &sin);
      return Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(&sin);
    } else {
      return Address::InstanceFactory::createInstancePtr<Address::Ipv6Instance>(*sin6, v6only);
    }
  }
  case AF_UNIX: {
    const struct sockaddr_un* sun = reinterpret_cast<const struct sockaddr_un*>(&ss);
    ASSERT(AF_UNIX == sun->sun_family);
    RELEASE_ASSERT(ss_len == 0 || static_cast<unsigned int>(ss_len) >=
                                      offsetof(struct sockaddr_un, sun_path) + 1,
                   "");
    return Address::InstanceFactory::createInstancePtr<Address::PipeInstance>(sun, ss_len);
  }
  default:
    return absl::InvalidArgumentError(fmt::format("Unexpected sockaddr family: {}", ss.ss_family));
  }
}

Address::InstanceConstSharedPtr addressFromSockAddrOrThrow(const sockaddr_storage& ss,
                                                           socklen_t ss_len, bool v6only) {
  // Though we don't have any test coverage where address validation in addressFromSockAddr() fails,
  // this code is called in worker thread and can throw in theory. In that case, the program will
  // crash due to uncaught exception. In practice, we don't expect any address validation in
  // addressFromSockAddr() to fail in worker thread.
  StatusOr<InstanceConstSharedPtr> address = addressFromSockAddr(ss, ss_len, v6only);
  return throwOnError(address);
}

Address::InstanceConstSharedPtr
addressFromSockAddrOrDie(const sockaddr_storage& ss, socklen_t ss_len, os_fd_t fd, bool v6only) {
  // Set v6only to false so that mapped-v6 address can be normalize to v4
  // address. Though dual stack may be disabled, it's still okay to assume the
  // address is from a dual stack socket. This is because mapped-v6 address
  // must come from a dual stack socket. An actual v6 address can come from
  // both dual stack socket and v6 only socket. If |peer_addr| is an actual v6
  // address and the socket is actually v6 only, the returned address will be
  // regarded as a v6 address from dual stack socket. However, this address is not going to be
  // used to create socket. Wrong knowledge of dual stack support won't hurt.
  StatusOr<Address::InstanceConstSharedPtr> address =
      Address::addressFromSockAddr(ss, ss_len, v6only);
  if (!address.ok()) {
    PANIC(fmt::format("Invalid address for fd: {}, error: {}", fd, address.status().ToString()));
  }
  return *address;
}

Ipv4Instance::Ipv4Instance(const sockaddr_in* address, const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  throwOnError(validateProtocolSupported());
  initHelper(address);
}

Ipv4Instance::Ipv4Instance(const std::string& address, const SocketInterface* sock_interface)
    : Ipv4Instance(address, 0, sockInterfaceOrDefault(sock_interface)) {}

Ipv4Instance::Ipv4Instance(const std::string& address, uint32_t port,
                           const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  throwOnError(validateProtocolSupported());
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  int rc = inet_pton(AF_INET, address.c_str(), &ip_.ipv4_.address_.sin_addr);
  if (1 != rc) {
    throwEnvoyExceptionOrPanic(fmt::format("invalid ipv4 address '{}'", address));
  }

  friendly_name_ = absl::StrCat(address, ":", port);
  ip_.friendly_address_ = address;
}

Ipv4Instance::Ipv4Instance(uint32_t port, const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  throwOnError(validateProtocolSupported());
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_.sin_family = AF_INET;
  ip_.ipv4_.address_.sin_port = htons(port);
  ip_.ipv4_.address_.sin_addr.s_addr = INADDR_ANY;
  friendly_name_ = absl::StrCat("0.0.0.0:", port);
  ip_.friendly_address_ = "0.0.0.0";
}

Ipv4Instance::Ipv4Instance(absl::Status& status, const sockaddr_in* address,
                           const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  status = validateProtocolSupported();
  if (!status.ok()) {
    return;
  }
  initHelper(address);
}

bool Ipv4Instance::operator==(const Instance& rhs) const {
  const Ipv4Instance* rhs_casted = dynamic_cast<const Ipv4Instance*>(&rhs);
  return (rhs_casted && (ip_.ipv4_.address() == rhs_casted->ip_.ipv4_.address()) &&
          (ip_.port() == rhs_casted->ip_.port()));
}

std::string Ipv4Instance::sockaddrToString(const sockaddr_in& addr) {
  static constexpr size_t BufferSize = 16; // enough space to hold an IPv4 address in string form
  char str[BufferSize];
  // Write backwards from the end of the buffer for simplicity.
  char* start = str + BufferSize;
  uint32_t ipv4_addr = ntohl(addr.sin_addr.s_addr);
  for (unsigned i = 4; i != 0; i--, ipv4_addr >>= 8) {
    uint32_t octet = ipv4_addr & 0xff;
    if (octet == 0) {
      ASSERT(start > str);
      *--start = '0';
    } else {
      do {
        ASSERT(start > str);
        *--start = '0' + (octet % 10);
        octet /= 10;
      } while (octet != 0);
    }
    if (i != 1) {
      ASSERT(start > str);
      *--start = '.';
    }
  }
  const std::string::size_type end = str + BufferSize - start;
  return {start, end};
}

namespace {
std::atomic<bool> force_ipv4_unsupported_for_test = false;
}

Cleanup Ipv4Instance::forceProtocolUnsupportedForTest(bool new_val) {
  bool old_val = force_ipv4_unsupported_for_test.load();
  force_ipv4_unsupported_for_test.store(new_val);
  return {[old_val]() { force_ipv4_unsupported_for_test.store(old_val); }};
}

absl::Status Ipv4Instance::validateProtocolSupported() {
  static const bool supported = SocketInterfaceSingleton::get().ipFamilySupported(AF_INET);
  if (supported && !force_ipv4_unsupported_for_test.load(std::memory_order_relaxed)) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError("IPv4 addresses are not supported on this machine");
}

void Ipv4Instance::initHelper(const sockaddr_in* address) {
  memset(&ip_.ipv4_.address_, 0, sizeof(ip_.ipv4_.address_));
  ip_.ipv4_.address_ = *address;
  ip_.friendly_address_ = sockaddrToString(*address);

  // Based on benchmark testing, this reserve+append implementation runs faster than absl::StrCat.
  fmt::format_int port(ntohs(address->sin_port));
  friendly_name_.reserve(ip_.friendly_address_.size() + 1 + port.size());
  friendly_name_.append(ip_.friendly_address_);
  friendly_name_.push_back(':');
  friendly_name_.append(port.data(), port.size());
}

absl::uint128 Ipv6Instance::Ipv6Helper::address() const {
  absl::uint128 result{0};
  static_assert(sizeof(absl::uint128) == 16, "The size of absl::uint128 is not 16.");
  safeMemcpyUnsafeSrc(&result, &address_.sin6_addr.s6_addr[0]);
  return result;
}

uint32_t Ipv6Instance::Ipv6Helper::scopeId() const { return address_.sin6_scope_id; }

uint32_t Ipv6Instance::Ipv6Helper::port() const { return ntohs(address_.sin6_port); }

bool Ipv6Instance::Ipv6Helper::v6only() const { return v6only_; };

std::string Ipv6Instance::Ipv6Helper::makeFriendlyAddress() const {
  char str[INET6_ADDRSTRLEN];
  const char* ptr = inet_ntop(AF_INET6, &address_.sin6_addr, str, INET6_ADDRSTRLEN);
  ASSERT(str == ptr);
  if (address_.sin6_scope_id != 0) {
    // Note that here we don't use the `if_indextoname` that will give a more user friendly
    // output just because in the past created a performance bottleneck if the machine had a
    // lot of IPv6 Link local addresses.
    return absl::StrCat(ptr, "%", scopeId());
  }
  return ptr;
}

InstanceConstSharedPtr Ipv6Instance::Ipv6Helper::v4CompatibleAddress() const {
  if (!v6only_ && IN6_IS_ADDR_V4MAPPED(&address_.sin6_addr)) {
    struct sockaddr_in sin;
    ipv6ToIpv4CompatibleAddress(&address_, &sin);
    auto addr = Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(&sin);
    return addr.ok() ? addr.value() : nullptr;
  }
  return nullptr;
}

InstanceConstSharedPtr Ipv6Instance::Ipv6Helper::addressWithoutScopeId() const {
  struct sockaddr_in6 ret_addr = address_;
  ret_addr.sin6_scope_id = 0;
  auto addr = Address::InstanceFactory::createInstancePtr<Address::Ipv6Instance>(ret_addr, v6only_);
  return addr.ok() ? addr.value() : nullptr;
}

Ipv6Instance::Ipv6Instance(const sockaddr_in6& address, bool v6only,
                           const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  throwOnError(validateProtocolSupported());
  initHelper(address, v6only);
}

Ipv6Instance::Ipv6Instance(const std::string& address, const SocketInterface* sock_interface)
    : Ipv6Instance(address, 0, sockInterfaceOrDefault(sock_interface)) {}

Ipv6Instance::Ipv6Instance(const std::string& address, uint32_t port,
                           const SocketInterface* sock_interface, bool v6only)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  throwOnError(validateProtocolSupported());
  sockaddr_in6 addr_in;
  memset(&addr_in, 0, sizeof(addr_in));
  addr_in.sin6_family = AF_INET6;
  addr_in.sin6_port = htons(port);
  if (!address.empty()) {
    if (1 != inet_pton(AF_INET6, address.c_str(), &addr_in.sin6_addr)) {
      throwEnvoyExceptionOrPanic(fmt::format("invalid ipv6 address '{}'", address));
    }
  } else {
    addr_in.sin6_addr = in6addr_any;
  }
  initHelper(addr_in, v6only);
}

Ipv6Instance::Ipv6Instance(uint32_t port, const SocketInterface* sock_interface)
    : Ipv6Instance("", port, sockInterfaceOrDefault(sock_interface)) {}

bool Ipv6Instance::operator==(const Instance& rhs) const {
  const auto* rhs_casted = dynamic_cast<const Ipv6Instance*>(&rhs);
  return (rhs_casted && (ip_.ipv6_.address() == rhs_casted->ip_.ipv6_.address()) &&
          (ip_.port() == rhs_casted->ip_.port()) &&
          (ip_.ipv6_.scopeId() == rhs_casted->ip_.ipv6_.scopeId()));
}

Ipv6Instance::Ipv6Instance(absl::Status& status, const sockaddr_in6& address, bool v6only,
                           const SocketInterface* sock_interface)
    : InstanceBase(Type::Ip, sockInterfaceOrDefault(sock_interface)) {
  status = validateProtocolSupported();
  if (!status.ok()) {
    return;
  }
  initHelper(address, v6only);
}

namespace {
std::atomic<bool> force_ipv6_unsupported_for_test = false;
}

Cleanup Ipv6Instance::forceProtocolUnsupportedForTest(bool new_val) {
  bool old_val = force_ipv6_unsupported_for_test.load();
  force_ipv6_unsupported_for_test.store(new_val);
  return {[old_val]() { force_ipv6_unsupported_for_test.store(old_val); }};
}

absl::Status Ipv6Instance::validateProtocolSupported() {
  static const bool supported = SocketInterfaceSingleton::get().ipFamilySupported(AF_INET6);
  if (supported && !force_ipv6_unsupported_for_test.load(std::memory_order_relaxed)) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError("IPv6 addresses are not supported on this machine");
}

void Ipv6Instance::initHelper(const sockaddr_in6& address, bool v6only) {
  ip_.ipv6_.address_ = address;
  ip_.friendly_address_ = ip_.ipv6_.makeFriendlyAddress();
  ip_.ipv6_.v6only_ = v6only;
  friendly_name_ = fmt::format("[{}]:{}", ip_.friendly_address_, ip_.port());
}

absl::StatusOr<std::unique_ptr<PipeInstance>>
PipeInstance::create(const sockaddr_un* address, socklen_t ss_len, mode_t mode,
                     const SocketInterface* sock_interface) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<PipeInstance>(
      new PipeInstance(creation_status, address, ss_len, mode, sock_interface));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

absl::StatusOr<std::unique_ptr<PipeInstance>>
PipeInstance::create(const std::string& pipe_path, mode_t mode,
                     const SocketInterface* sock_interface) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<PipeInstance>(
      new PipeInstance(pipe_path, mode, sock_interface, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

PipeInstance::PipeInstance(const std::string& pipe_path, mode_t mode,
                           const SocketInterface* sock_interface, absl::Status& creation_status)
    : InstanceBase(Type::Pipe, sockInterfaceOrDefault(sock_interface)) {
  if (pipe_path.size() >= sizeof(pipe_.address_.sun_path)) {
    creation_status = absl::InvalidArgumentError(
        fmt::format("Path \"{}\" exceeds maximum UNIX domain socket path size of {}.", pipe_path,
                    sizeof(pipe_.address_.sun_path)));
    return;
  }
  memset(&pipe_.address_, 0, sizeof(pipe_.address_));
  pipe_.address_.sun_family = AF_UNIX;
  if (pipe_path[0] == '@') {
    // This indicates an abstract namespace.
    // In this case, null bytes in the name have no special significance, and so we copy all
    // characters of pipe_path to sun_path, including null bytes in the name. The pathname must also
    // be null terminated. The friendly name is the address path with embedded nulls replaced with
    // '@' for consistency with the first character.
#if !defined(__linux__)
    creation_status =
        absl::InvalidArgumentError("Abstract AF_UNIX sockets are only supported on linux.");
    return;
#endif
    if (mode != 0) {
      creation_status = absl::InvalidArgumentError("Cannot set mode for Abstract AF_UNIX sockets");
      return;
    }
    pipe_.abstract_namespace_ = true;
    pipe_.address_length_ = pipe_path.size();
    // The following statement is safe since pipe_path size was checked at the beginning of this
    // function
    memcpy(&pipe_.address_.sun_path[0], pipe_path.data(), pipe_path.size()); // NOLINT(safe-memcpy)
    pipe_.address_.sun_path[0] = '\0';
    pipe_.address_.sun_path[pipe_path.size()] = '\0';
    friendly_name_ = friendlyNameFromAbstractPath(
        absl::string_view(pipe_.address_.sun_path, pipe_.address_length_));
  } else {
    // return an error if the pipe path has an embedded null character.
    if (pipe_path.size() != strlen(pipe_path.c_str())) {
      creation_status = absl::InvalidArgumentError(
          "UNIX domain socket pathname contains embedded null characters");
      return;
    }
    StringUtil::strlcpy(&pipe_.address_.sun_path[0], pipe_path.c_str(),
                        sizeof(pipe_.address_.sun_path));
    friendly_name_ = pipe_.address_.sun_path;
  }
  pipe_.mode_ = mode;
}

PipeInstance::PipeInstance(absl::Status& error, const sockaddr_un* address, socklen_t ss_len,
                           mode_t mode, const SocketInterface* sock_interface)
    : InstanceBase(Type::Pipe, sockInterfaceOrDefault(sock_interface)) {
  if (address->sun_path[0] == '\0') {
#if !defined(__linux__)
    error = absl::FailedPreconditionError("Abstract AF_UNIX sockets are only supported on linux.");
    return;
#endif
    RELEASE_ASSERT(static_cast<unsigned int>(ss_len) >= offsetof(struct sockaddr_un, sun_path) + 1,
                   "");
    pipe_.abstract_namespace_ = true;
    pipe_.address_length_ = ss_len - offsetof(struct sockaddr_un, sun_path);
  }
  error = initHelper(address, mode);
}

bool PipeInstance::operator==(const Instance& rhs) const { return asString() == rhs.asString(); }

absl::Status PipeInstance::initHelper(const sockaddr_un* address, mode_t mode) {
  pipe_.address_ = *address;
  if (pipe_.abstract_namespace_) {
    if (mode != 0) {
      return absl::FailedPreconditionError("Cannot set mode for Abstract AF_UNIX sockets");
    }
    // Replace all null characters with '@' in friendly_name_.
    friendly_name_ = friendlyNameFromAbstractPath(
        absl::string_view(pipe_.address_.sun_path, pipe_.address_length_));
  } else {
    friendly_name_ = address->sun_path;
  }
  pipe_.mode_ = mode;
  return absl::OkStatus();
}

EnvoyInternalInstance::EnvoyInternalInstance(const std::string& address_id,
                                             const std::string& endpoint_id,
                                             const SocketInterface* sock_interface)
    : InstanceBase(Type::EnvoyInternal, sockInterfaceOrDefault(sock_interface)),
      internal_address_(address_id, endpoint_id) {
  friendly_name_ = absl::StrCat("envoy://", address_id, "/", endpoint_id);
}

bool EnvoyInternalInstance::operator==(const Instance& rhs) const {
  return rhs.type() == Type::EnvoyInternal && asString() == rhs.asString();
}

} // namespace Address
} // namespace Network
} // namespace Envoy
