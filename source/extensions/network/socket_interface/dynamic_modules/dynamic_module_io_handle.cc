#include "source/extensions/network/socket_interface/dynamic_modules/dynamic_module_io_handle.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>

#include "envoy/common/platform.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/utility.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace Network {
namespace SocketInterface {
namespace DynamicModules {

namespace {

// The handle's base logs to the io logger, so module failures log to dynamic_modules instead.
#define DM_SOCKET_LOG_PERIODIC(LEVEL, ...)                                                         \
  ENVOY_LOG_PERIODIC_TO_LOGGER(                                                                    \
      Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules), LEVEL,                  \
      std::chrono::seconds(10), ##__VA_ARGS__)

Api::IoCallUint64Result connectionResetResult() {
  return {0, Network::IoSocketError::create(SOCKET_ERROR_CONNRESET)};
}

uint32_t toFileReadyEvents(uint32_t events) {
  uint32_t file_events = 0;
  if (events & envoy_dynamic_module_type_socket_interface_io_event_Read) {
    file_events |= Event::FileReadyType::Read;
  }
  if (events & envoy_dynamic_module_type_socket_interface_io_event_Write) {
    file_events |= Event::FileReadyType::Write;
  }
  return file_events;
}

// Converts an address the module reported for a handed-over connection. Returns nullptr when the
// module reported none.
absl::StatusOr<Network::Address::InstanceConstSharedPtr>
toAddress(const envoy_dynamic_module_type_socket_interface_address& address, int domain,
          bool socket_v6only) {
  if (address.address.ptr == nullptr || address.address.length == 0) {
    return Network::Address::InstanceConstSharedPtr{};
  }
  const std::string value(address.address.ptr, address.address.length);
  if (domain == AF_UNIX) {
    auto pipe_or = Network::Address::PipeInstance::create(value);
    RETURN_IF_NOT_OK_REF(pipe_or.status());
    return Network::Address::InstanceConstSharedPtr{std::move(pipe_or.value())};
  }
  if (address.port > UINT16_MAX) {
    return absl::InvalidArgumentError(absl::StrCat("invalid port ", address.port));
  }
  auto ip = Network::Utility::parseInternetAddressNoThrow(
      value, static_cast<uint16_t>(address.port), socket_v6only);
  if (ip == nullptr ||
      (ip->ip()->version() == Network::Address::IpVersion::v6) != (domain == AF_INET6)) {
    return absl::InvalidArgumentError(absl::StrCat("invalid address ", value));
  }
  // Match the kernel path, which turns a v4-mapped address on a dual-stack socket into IPv4.
  if (domain == AF_INET6 && !socket_v6only) {
    if (auto v4 = ip->ip()->ipv6()->v4CompatibleAddress(); v4 != nullptr) {
      return v4;
    }
  }
  return ip;
}

/**
 * A socket of the requested family and type whose operations fail. See makeFailedIoHandle().
 */
class FailedIoHandle : public Network::IoSocketHandleImpl {
public:
  FailedIoHandle(os_fd_t fd, bool socket_v6only, int domain, Network::Socket::Type socket_type)
      : Network::IoSocketHandleImpl(fd, socket_v6only, domain), socket_type_(socket_type) {}

  Api::IoCallUint64Result readv(uint64_t, Buffer::RawSlice*, uint64_t) override {
    return failedIo();
  }
  Api::IoCallUint64Result writev(const Buffer::RawSlice*, uint64_t) override { return failedIo(); }
  Api::IoCallUint64Result recv(void*, size_t, int) override { return failedIo(); }
  Api::IoCallUint64Result send(const void*, size_t) override { return failedIo(); }
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice*, uint64_t, int,
                                  const Network::Address::Ip*,
                                  const Network::Address::Instance&) override {
    return failedIo();
  }
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice*, const uint64_t, uint32_t,
                                  const Network::IoHandle::UdpSaveCmsgConfig&,
                                  RecvMsgOutput&) override {
    return failedIo();
  }
  Api::IoCallUint64Result recvmmsg(RawSliceArrays&, uint32_t,
                                   const Network::IoHandle::UdpSaveCmsgConfig&,
                                   RecvMsgOutput&) override {
    return failedIo();
  }
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr) override {
    return {-1, SOCKET_ERROR_NOT_SUP};
  }
  Api::SysCallIntResult listen(int) override { return {-1, SOCKET_ERROR_NOT_SUP}; }
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr) override {
    return {-1, SOCKET_ERROR_NOT_SUP};
  }
  Network::IoHandlePtr accept(struct sockaddr*, socklen_t*) override { return nullptr; }
  Network::IoHandlePtr duplicate() override {
    return makeFailedIoHandle(domain_.value_or(AF_INET), socket_type_, socket_v6only_);
  }

private:
  static Api::IoCallUint64Result failedIo() {
    return {0, Network::IoSocketError::create(SOCKET_ERROR_NOT_SUP)};
  }

  const Network::Socket::Type socket_type_;
};

} // namespace

Network::IoHandlePtr makeFailedIoHandle(int domain, Network::Socket::Type socket_type,
                                        bool socket_v6only) {
  const int type = socket_type == Network::Socket::Type::Stream ? SOCK_STREAM : SOCK_DGRAM;
  const Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().socket(domain, type, 0);
  if (!SOCKET_VALID(result.return_value_)) {
    IS_ENVOY_BUG(fmt::format("socket(2) failed, got error: {}", errorDetails(result.errno_)));
    return nullptr;
  }
  return std::make_unique<FailedIoHandle>(result.return_value_, socket_v6only, domain, socket_type);
}

// =============================================================================
// SocketSharedState
// =============================================================================

void SocketSharedState::report(uint32_t file_events) {
  absl::MutexLock lock(&mutex_);
  if (closed_ || file_events == 0) {
    return;
  }
  pending_ |= file_events;
  ++report_sequence_;
  if (post_in_flight_ || dispatcher_ == nullptr) {
    // A delivery is already queued, or the next registration delivers the pending events.
    return;
  }
  post_in_flight_ = true;
  // Posting under the lock keeps the dispatcher alive: the registration that owns it is cleared
  // under the same lock before the dispatcher goes away.
  dispatcher_->post([weak_state = weak_from_this(), generation = generation_]() {
    if (auto state = weak_state.lock()) {
      state->onPosted(generation);
    }
  });
}

void SocketSharedState::onPosted(uint64_t generation) {
  DynamicModuleIoHandle* handle;
  {
    absl::MutexLock lock(&mutex_);
    post_in_flight_ = false;
    if (generation != generation_ || handle_ == nullptr) {
      return;
    }
    handle = handle_;
  }
  // The post runs on the worker of the registration it was made for, which is the only thread
  // that ends that registration, so the handle is alive here.
  handle->deliverPending();
}

void SocketSharedState::setRegistration(DynamicModuleIoHandle& handle,
                                        Event::Dispatcher& dispatcher) {
  absl::MutexLock lock(&mutex_);
  handle_ = &handle;
  dispatcher_ = &dispatcher;
  ++generation_;
  post_in_flight_ = false;
}

void SocketSharedState::clearRegistration() {
  absl::MutexLock lock(&mutex_);
  handle_ = nullptr;
  dispatcher_ = nullptr;
  ++generation_;
  post_in_flight_ = false;
}

void SocketSharedState::markClosed() {
  absl::MutexLock lock(&mutex_);
  closed_ = true;
  pending_ = 0;
  handle_ = nullptr;
  dispatcher_ = nullptr;
  ++generation_;
}

uint32_t SocketSharedState::pending() const {
  absl::MutexLock lock(&mutex_);
  return pending_;
}

uint64_t SocketSharedState::reportSequence() const {
  absl::MutexLock lock(&mutex_);
  return report_sequence_;
}

void SocketSharedState::clearPending(uint32_t file_events, uint64_t sequence) {
  absl::MutexLock lock(&mutex_);
  if (report_sequence_ == sequence) {
    pending_ &= ~file_events;
  }
}

void SocketReadyHandle::commit(uint32_t events) {
  if (auto state = state_.lock()) {
    state->report(toFileReadyEvents(events));
  }
}

// =============================================================================
// DynamicModuleIoHandle
// =============================================================================

DynamicModuleIoHandle::DynamicModuleIoHandle(
    DynamicModuleSocketInterfaceConfigSharedPtr config, os_fd_t fd, bool socket_v6only,
    std::optional<int> domain, const Network::SocketCreationOptions& options,
    envoy_dynamic_module_type_socket_interface_socket_module_ptr socket_module_ptr, uint32_t flags,
    bool already_connected, bool connecting)
    : Network::IoSocketHandleImpl(fd, socket_v6only, domain, options.max_addresses_cache_size_),
      config_(std::move(config)), shared_state_(std::make_shared<SocketSharedState>()),
      max_addresses_cache_size_(options.max_addresses_cache_size_),
      socket_module_ptr_(socket_module_ptr), flags_(flags), already_connected_(already_connected),
      connecting_(connecting) {}

DynamicModuleIoHandle::~DynamicModuleIoHandle() {
  // close() closes the fd before the module's socket_close, as the ABI promises.
  close();
  shared_state_->markClosed();
  releaseSocketModulePtr();
}

bool DynamicModuleIoHandle::delegatesIo() const {
  return flags_ & envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo;
}

bool DynamicModuleIoHandle::handsOverConnections() const {
  return flags_ & envoy_dynamic_module_type_socket_interface_socket_flag_HandsOverConnections;
}

void DynamicModuleIoHandle::attachReadyHandle() {
  ASSERT(flags_ != 0 && socket_module_ptr_ != nullptr);
  config_->on_set_socket_handle_(config_->in_module_config_, socket_module_ptr_,
                                 new SocketReadyHandle(shared_state_->weak_from_this()));
}

void DynamicModuleIoHandle::releaseSocketModulePtr() {
  if (socket_module_ptr_ != nullptr) {
    config_->on_socket_close_(config_->in_module_config_, socket_module_ptr_);
    socket_module_ptr_ = nullptr;
  }
}

void DynamicModuleIoHandle::deliverPending() {
  if (!registered_ || connecting_) {
    return;
  }
  const uint32_t events = shared_state_->pending() & enabled_events_;
  if (events == 0) {
    return;
  }
  // A delivered Write is consumed, since Envoy writes only when it has data; a Read stays pending
  // until a hook drains it.
  if (events & Event::FileReadyType::Write) {
    shared_state_->clearPending(Event::FileReadyType::Write, shared_state_->reportSequence());
  }
  Network::IoSocketHandleImpl::activateFileEvents(events);
}

Api::IoCallUint64Result
DynamicModuleIoHandle::readFromModule(envoy_dynamic_module_type_socket_interface_io_slice* slices,
                                      size_t num_slices, uint64_t total_length, bool peek) {
  const uint64_t sequence = shared_state_->reportSequence();
  uint64_t bytes_transferred = 0;
  const auto status = config_->on_io_read_(config_->in_module_config_, socket_module_ptr_, slices,
                                           num_slices, peek, &bytes_transferred);
  switch (status) {
  case envoy_dynamic_module_type_socket_interface_io_status_Success:
    ENVOY_BUG(bytes_transferred <= total_length,
              fmt::format("Module read {} bytes but only offered capacity for {}",
                          bytes_transferred, total_length));
    bytes_transferred = std::min(bytes_transferred, total_length);
    if (bytes_transferred == 0) {
      // The end of the stream drains the readiness.
      shared_state_->clearPending(Event::FileReadyType::Read, sequence);
    } else if (!peek) {
      progress_ = true;
    }
    return {bytes_transferred, Api::IoError::none()};
  case envoy_dynamic_module_type_socket_interface_io_status_Again:
    shared_state_->clearPending(Event::FileReadyType::Read, sequence);
    // EAGAIN is frequent enough that its memory allocation should be avoided.
    return {0, Network::IoSocketError::getIoSocketEagainError()};
  case envoy_dynamic_module_type_socket_interface_io_status_Error:
  case envoy_dynamic_module_type_socket_interface_io_status_NotImplemented:
    return connectionResetResult();
  }
  IS_ENVOY_BUG("Unexpected socket interface I/O status");
  return connectionResetResult();
}

Api::IoCallUint64Result DynamicModuleIoHandle::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                     uint64_t num_slice) {
  if (!delegatesIo()) {
    return Network::IoSocketHandleImpl::readv(max_length, slices, num_slice);
  }
  if (max_length == 0 || num_slice == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  absl::FixedArray<envoy_dynamic_module_type_socket_interface_io_slice> io_slices(num_slice);
  uint64_t num_slices_to_read = 0;
  uint64_t total_length = 0;
  for (; num_slices_to_read < num_slice && total_length < max_length; num_slices_to_read++) {
    io_slices[num_slices_to_read].ptr = static_cast<char*>(slices[num_slices_to_read].mem_);
    const size_t length =
        std::min(slices[num_slices_to_read].len_, static_cast<size_t>(max_length - total_length));
    io_slices[num_slices_to_read].length = length;
    total_length += length;
  }
  return readFromModule(io_slices.data(), num_slices_to_read, total_length, /*peek=*/false);
}

Api::IoCallUint64Result DynamicModuleIoHandle::recv(void* buffer, size_t length, int flags) {
  if (!delegatesIo()) {
    return Network::IoSocketHandleImpl::recv(buffer, length, flags);
  }
  if (length == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  envoy_dynamic_module_type_socket_interface_io_slice slice{static_cast<char*>(buffer), length};
  return readFromModule(&slice, 1, length, (flags & MSG_PEEK) != 0);
}

Api::IoCallUint64Result DynamicModuleIoHandle::writev(const Buffer::RawSlice* slices,
                                                      uint64_t num_slice) {
  if (!delegatesIo()) {
    return Network::IoSocketHandleImpl::writev(slices, num_slice);
  }
  absl::FixedArray<envoy_dynamic_module_type_envoy_buffer> buffers(num_slice);
  uint64_t num_buffers = 0;
  uint64_t total_length = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      buffers[num_buffers].ptr = static_cast<const char*>(slices[i].mem_);
      buffers[num_buffers].length = slices[i].len_;
      total_length += slices[i].len_;
      num_buffers++;
    }
  }
  if (num_buffers == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  uint64_t bytes_transferred = 0;
  const auto status = config_->on_io_write_(config_->in_module_config_, socket_module_ptr_,
                                            buffers.data(), num_buffers, &bytes_transferred);
  switch (status) {
  case envoy_dynamic_module_type_socket_interface_io_status_Success:
    ENVOY_BUG(bytes_transferred <= total_length,
              fmt::format("Module wrote {} bytes but was only offered {}", bytes_transferred,
                          total_length));
    bytes_transferred = std::min(bytes_transferred, total_length);
    if (bytes_transferred == 0) {
      // A write that takes nothing would otherwise be retried at once, forever.
      return {0, Network::IoSocketError::getIoSocketEagainError()};
    }
    return {bytes_transferred, Api::IoError::none()};
  case envoy_dynamic_module_type_socket_interface_io_status_Again:
    return {0, Network::IoSocketError::getIoSocketEagainError()};
  case envoy_dynamic_module_type_socket_interface_io_status_Error:
  case envoy_dynamic_module_type_socket_interface_io_status_NotImplemented:
    return connectionResetResult();
  }
  IS_ENVOY_BUG("Unexpected socket interface I/O status");
  return connectionResetResult();
}

Api::IoCallUint64Result DynamicModuleIoHandle::send(const void* buffer, size_t length) {
  if (!delegatesIo()) {
    return Network::IoSocketHandleImpl::send(buffer, length);
  }
  Buffer::RawSlice slice{const_cast<void*>(buffer), length};
  return writev(&slice, 1);
}

Api::IoCallUint64Result DynamicModuleIoHandle::close() {
  if (!SOCKET_VALID(fd_)) {
    return Api::ioCallUint64ResultNoError();
  }
  shared_state_->markClosed();
  registered_ = false;
  ++registration_generation_;
  auto result = Network::IoSocketHandleImpl::close();
  releaseSocketModulePtr();
  return result;
}

Api::SysCallIntResult DynamicModuleIoHandle::shutdown(int how) {
  if (!delegatesIo() || config_->on_io_shutdown_ == nullptr) {
    return Network::IoSocketHandleImpl::shutdown(how);
  }
  const auto status = config_->on_io_shutdown_(config_->in_module_config_, socket_module_ptr_, how);
  if (status == envoy_dynamic_module_type_socket_interface_io_status_Success) {
    return {0, 0};
  }
  if (status == envoy_dynamic_module_type_socket_interface_io_status_NotImplemented) {
    return Network::IoSocketHandleImpl::shutdown(how);
  }
  return {-1, SOCKET_ERROR_CONNRESET};
}

Api::SysCallIntResult
DynamicModuleIoHandle::bind(Network::Address::InstanceConstSharedPtr address) {
  if (handsOverConnections()) {
    // Connections come from the module, so the socket never takes the listener's address.
    return {0, 0};
  }
  return Network::IoSocketHandleImpl::bind(address);
}

Api::SysCallIntResult DynamicModuleIoHandle::listen(int backlog) {
  if (handsOverConnections()) {
    return {0, 0};
  }
  return Network::IoSocketHandleImpl::listen(backlog);
}

Api::SysCallIntResult
DynamicModuleIoHandle::connect(Network::Address::InstanceConstSharedPtr address) {
  if (already_connected_) {
    // Connecting an already connected socket fails with "already connected". Envoy still waits
    // for the socket to become writable, which completes a connect the module left in flight.
    was_connected_ = true;
    return {0, 0};
  }
  return Network::IoSocketHandleImpl::connect(address);
}

void DynamicModuleIoHandle::rejectAccepted(
    os_fd_t fd, envoy_dynamic_module_type_socket_interface_socket_module_ptr module_ptr,
    absl::string_view reason) {
  if (SOCKET_VALID(fd)) {
    Api::OsSysCallsSingleton::get().close(fd);
  }
  if (module_ptr != nullptr) {
    config_->on_socket_close_(config_->in_module_config_, module_ptr);
  }
  config_->stats().accept_failed_.inc();
  DM_SOCKET_LOG_PERIODIC(error, "Dynamic module socket interface '{}' handed over {}",
                         config_->socketInterfaceName(), reason);
}

Network::IoHandlePtr DynamicModuleIoHandle::accept(struct sockaddr* addr, socklen_t* addrlen) {
  if (!handsOverConnections()) {
    return Network::IoSocketHandleImpl::accept(addr, addrlen);
  }
  const uint64_t sequence = shared_state_->reportSequence();
  int64_t fd = INVALID_SOCKET;
  uint32_t flags = 0;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr accepted_module_ptr = nullptr;
  envoy_dynamic_module_type_socket_interface_address local = {};
  envoy_dynamic_module_type_socket_interface_address peer = {};
  const auto status = config_->on_accept_(config_->in_module_config_, socket_module_ptr_, &fd,
                                          &flags, &accepted_module_ptr, &local, &peer);
  if (status == envoy_dynamic_module_type_socket_interface_io_status_Again) {
    shared_state_->clearPending(Event::FileReadyType::Read, sequence);
    return nullptr;
  }
  if (status != envoy_dynamic_module_type_socket_interface_io_status_Success) {
    config_->stats().accept_failed_.inc();
    DM_SOCKET_LOG_PERIODIC(warn, "Dynamic module socket interface '{}' failed to accept",
                           config_->socketInterfaceName());
    return nullptr;
  }
  // Envoy stops this accept loop on a rejected connection, so let the next one in after it.
  progress_ = true;

  const int domain = domain_.value_or(AF_INET);
  const os_fd_t accepted_fd = toOsFd(fd);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  if (flags & ~envoy_dynamic_module_type_socket_interface_socket_flag_DelegateIo) {
    rejectAccepted(accepted_fd, accepted_module_ptr, "a connection with an invalid flag");
    return nullptr;
  }
  if (flags != 0 && (!config_->implementsIoHooks() || accepted_module_ptr == nullptr ||
                     !config_->hotRestartDisabled())) {
    rejectAccepted(accepted_fd, accepted_module_ptr,
                   "a delegated connection without the I/O hooks or a per-socket state, or "
                   "while hot restart is enabled");
    return nullptr;
  }
  sockaddr_storage own_addr = {};
  socklen_t own_addr_len = sizeof(own_addr);
  int socket_type = 0;
  socklen_t socket_type_len = sizeof(socket_type);
  if (!SOCKET_VALID(accepted_fd) ||
      os_sys_calls.getsockname(accepted_fd, reinterpret_cast<sockaddr*>(&own_addr), &own_addr_len)
              .return_value_ != 0 ||
      own_addr.ss_family != domain ||
      os_sys_calls.getsockopt(accepted_fd, SOL_SOCKET, SO_TYPE, &socket_type, &socket_type_len)
              .return_value_ != 0 ||
      socket_type != SOCK_STREAM) {
    rejectAccepted(accepted_fd, accepted_module_ptr,
                   "a file descriptor that is not a stream socket of the listener's family");
    return nullptr;
  }
  sockaddr_storage peer_addr = {};
  socklen_t peer_addr_len = sizeof(peer_addr);
  if (os_sys_calls.getpeername(accepted_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len)
          .return_value_ != 0) {
    rejectAccepted(accepted_fd, accepted_module_ptr, "an unconnected socket");
    return nullptr;
  }

  auto local_or = toAddress(local, domain, socket_v6only_);
  auto peer_or = toAddress(peer, domain, socket_v6only_);
  if (!local_or.ok() || !peer_or.ok()) {
    rejectAccepted(accepted_fd, accepted_module_ptr,
                   absl::StrCat("a connection with ", !local_or.ok() ? local_or.status().message()
                                                                     : peer_or.status().message()));
    return nullptr;
  }
  // Envoy names an AF_UNIX connection's peer after the connection's own socket, so an unnamed
  // one needs the module to report the peer.
  if (domain == AF_UNIX && *peer_or == nullptr &&
      own_addr_len <= static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1)) {
    rejectAccepted(accepted_fd, accepted_module_ptr,
                   "an unnamed AF_UNIX socket without a peer address");
    return nullptr;
  }

  if (*peer_or != nullptr) {
    peer_addr_len = (*peer_or)->sockAddrLen();
    memcpy(&peer_addr, (*peer_or)->sockAddr(), peer_addr_len); // NOLINT(safe-memcpy)
  }
  memcpy(addr, &peer_addr, std::min(peer_addr_len, *addrlen)); // NOLINT(safe-memcpy)
  *addrlen = peer_addr_len;

  auto handle = std::make_unique<DynamicModuleIoHandle>(
      config_, accepted_fd, socket_v6only_, domain,
      Network::SocketCreationOptions{false, max_addresses_cache_size_}, accepted_module_ptr, flags,
      /*already_connected=*/true, /*connecting=*/false);
  handle->setAddresses(std::move(*local_or), std::move(*peer_or));
  if (flags != 0) {
    handle->attachReadyHandle();
  }
  return handle;
}

Network::IoHandlePtr DynamicModuleIoHandle::duplicate() {
  if (flags_ == 0) {
    return Network::IoSocketHandleImpl::duplicate();
  }
  const int domain = domain_.value_or(AF_INET);
  int64_t fd = INVALID_SOCKET;
  envoy_dynamic_module_type_socket_interface_socket_module_ptr duplicate_module_ptr = nullptr;
  const auto status = config_->on_socket_duplicate_(config_->in_module_config_, socket_module_ptr_,
                                                    &fd, &duplicate_module_ptr);
  sockaddr_storage own_addr = {};
  socklen_t own_addr_len = sizeof(own_addr);
  const os_fd_t duplicate_fd = toOsFd(fd);
  int socket_type = 0;
  socklen_t socket_type_len = sizeof(socket_type);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  // The copy must be a separate fd of the same kind, or two handles would close one fd.
  if (status == envoy_dynamic_module_type_socket_interface_io_status_Success &&
      duplicate_module_ptr != nullptr && SOCKET_VALID(duplicate_fd) && duplicate_fd != fd_ &&
      os_sys_calls.getsockname(duplicate_fd, reinterpret_cast<sockaddr*>(&own_addr), &own_addr_len)
              .return_value_ == 0 &&
      own_addr.ss_family == domain &&
      os_sys_calls.getsockopt(duplicate_fd, SOL_SOCKET, SO_TYPE, &socket_type, &socket_type_len)
              .return_value_ == 0 &&
      socket_type == SOCK_STREAM) {
    auto handle = std::make_unique<DynamicModuleIoHandle>(
        config_, duplicate_fd, socket_v6only_, domain,
        Network::SocketCreationOptions{false, max_addresses_cache_size_}, duplicate_module_ptr,
        flags_, already_connected_, /*connecting=*/false);
    handle->attachReadyHandle();
    return handle;
  }
  if (status == envoy_dynamic_module_type_socket_interface_io_status_Success) {
    if (SOCKET_VALID(duplicate_fd) && duplicate_fd != fd_) {
      Api::OsSysCallsSingleton::get().close(duplicate_fd);
    }
    if (duplicate_module_ptr != nullptr) {
      config_->on_socket_close_(config_->in_module_config_, duplicate_module_ptr);
    }
  }
  config_->stats().duplicate_failed_.inc();
  DM_SOCKET_LOG_PERIODIC(error, "Dynamic module socket interface '{}' failed to copy a socket",
                         config_->socketInterfaceName());
  return makeFailedIoHandle(domain, Network::Socket::Type::Stream, socket_v6only_);
}

void DynamicModuleIoHandle::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                Event::FileReadyCb cb,
                                                Event::FileTriggerType trigger, uint32_t events) {
  if (flags_ == 0) {
    Network::IoSocketHandleImpl::initializeFileEvent(dispatcher, std::move(cb), trigger, events);
    return;
  }
  Network::IoSocketHandleImpl::initializeFileEvent(
      dispatcher,
      [this, cb = std::move(cb)](uint32_t file_events) -> absl::Status {
        // `cb` can close the socket or reset its events, which frees this closure, so only locals
        // are used after it runs.
        DynamicModuleIoHandle* self = this;
        const uint64_t generation = self->registration_generation_;
        bool connected_now = false;
        if (self->connecting_ && (file_events & Event::FileReadyType::Write)) {
          // While connecting, Envoy injects nothing, so this Write is the kernel's.
          self->connecting_ = false;
          connected_now = true;
        }
        self->progress_ = false;
        absl::Status status = cb(file_events);
        if (self->registration_generation_ != generation) {
          return status;
        }
        if (connected_now || self->progress_) {
          self->deliverPending();
        }
        return status;
      },
      trigger, handsOverConnections() ? 0 : events);
  registered_ = true;
  ++registration_generation_;
  enabled_events_ = events;
  shared_state_->setRegistration(*this, dispatcher);
  deliverPending();
}

void DynamicModuleIoHandle::enableFileEvents(uint32_t events) {
  if (flags_ == 0) {
    Network::IoSocketHandleImpl::enableFileEvents(events);
    return;
  }
  enabled_events_ = events;
  // A socket that hands over connections never wants kernel events on its own file descriptor.
  Network::IoSocketHandleImpl::enableFileEvents(handsOverConnections() ? 0 : events);
  deliverPending();
}

void DynamicModuleIoHandle::resetFileEvents() {
  if (flags_ != 0) {
    registered_ = false;
    ++registration_generation_;
    shared_state_->clearRegistration();
  }
  Network::IoSocketHandleImpl::resetFileEvents();
}

absl::StatusOr<Network::Address::InstanceConstSharedPtr> DynamicModuleIoHandle::localAddress() {
  if (local_address_ != nullptr) {
    return local_address_;
  }
  return Network::IoSocketHandleImpl::localAddress();
}

absl::StatusOr<Network::Address::InstanceConstSharedPtr> DynamicModuleIoHandle::peerAddress() {
  if (peer_address_ != nullptr) {
    return peer_address_;
  }
  return Network::IoSocketHandleImpl::peerAddress();
}

} // namespace DynamicModules
} // namespace SocketInterface
} // namespace Network
} // namespace Extensions
} // namespace Envoy
