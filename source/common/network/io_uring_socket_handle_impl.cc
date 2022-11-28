#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include <asm-generic/errno-base.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "io_socket_handle_impl.h"
#include "io_uring_socket_handle_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/io/io_uring.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {

namespace {

constexpr socklen_t udsAddressLength() { return sizeof(sa_family_t); }

} // namespace

IoUringSocketHandleImpl::IoUringSocketHandleImpl(const uint32_t read_buffer_size,
                                                 Io::IoUringFactory& io_uring_factory,
                                                 os_fd_t fd, bool socket_v6only,
                                                 absl::optional<int> domain,
                                                 bool is_server_socket)
    : read_buffer_size_(read_buffer_size), io_uring_factory_(io_uring_factory), fd_(fd),
      socket_v6only_(socket_v6only), domain_(domain) {
  ENVOY_LOG(debug, "construct io uring socket handle, fd = {}, is_server_socket = {}", fd_, is_server_socket);
  if (is_server_socket) {
    io_uring_socket_type_ = IoUringSocketType::Server;
  }
}

IoUringSocketHandleImpl::~IoUringSocketHandleImpl() {
  if (SOCKET_VALID(fd_)) {
    if (io_uring_socket_type_ == IoUringSocketType::Client) {
      shadow_io_handle_->close();
      return;
    }
    // The TLS slot has been shut down by this moment with IoUring wiped out, thus
    // better use this posix system call instead of IoUringSocketHandleImpl::close().
    ::close(fd_);
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  ENVOY_LOG(debug, "close, fd = {}", fd_);

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client: {
      // Fall back to shadow io handle if client socket disabled.
      // This will be removed when all the debug work done.
      ENVOY_LOG(trace, "close the client socket");
      if (shadow_io_handle_ == nullptr) {
        ::close(fd_);
        SET_SOCKET_INVALID(fd_);
        return Api::ioCallUint64ResultNoError();
      }
      SET_SOCKET_INVALID(fd_);
      return shadow_io_handle_->close();
    }
    case IoUringSocketType::Server: {
      // Fall back to shadow io handle if server socket disabled.
      // This will be removed when all the debug work done.
      if (!enable_server_socket_) {
        ENVOY_LOG(trace, "close the server socket, fd = {}", fd_);
        if (shadow_io_handle_ == nullptr) {
          ::close(fd_);
          SET_SOCKET_INVALID(fd_);
          return Api::ioCallUint64ResultNoError();
        }
        SET_SOCKET_INVALID(fd_);
        return shadow_io_handle_->close(); 
      }
      // If server socket is enabled, then execute the same code with listen socket.
    }
    case IoUringSocketType::Listen: {
      ENVOY_LOG(trace, "close the socket, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

      // There could be chance the listen socket was close before initialzie file event.
      if (!io_uring_worker_.has_value()) {
        ::close(fd_);
        SET_SOCKET_INVALID(fd_);
        return Api::ioCallUint64ResultNoError();
      }
      io_uring_worker_.ref().closeSocket(fd_);
      SET_SOCKET_INVALID(fd_);
      return Api::ioCallUint64ResultNoError();
    }
    case IoUringSocketType::Unknown: {
      // There is case the socket will be closed directly without initialize any event.
      ::close(fd_);
      SET_SOCKET_INVALID(fd_);
      return Api::ioCallUint64ResultNoError();
    }
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool IoUringSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }

Api::IoCallUint64Result
IoUringSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client:
      // Fall back to shadow io handle if client socket disabled.
      // This will be removed when all the debug work done.
      return shadow_io_handle_->readv(max_length, slices, num_slice);
    case IoUringSocketType::Server: {
      // Fall back to shadow io handle if server socket disabled.
      // This will be removed when all the debug work done.
      if (!enable_server_socket_) {
        return shadow_io_handle_->readv(max_length, slices, num_slice);
      } else {
        ENVOY_LOG(debug, "readv, result = {}, fd = {}", read_param_->result_, fd_);

        if (read_param_ == absl::nullopt) {
          return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                      IoSocketError::deleteIoError)};
        }

        if (read_param_->result_ == 0) {
          ENVOY_LOG(debug, "readv remote close");
          return Api::ioCallUint64ResultNoError();
        }

        if (read_param_->result_ == -EAGAIN) {
          ENVOY_LOG(debug, "read eagain");
          return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                    IoSocketError::deleteIoError)};
        }
      
        if (read_param_->result_ < 0) {
          return {0, Api::IoErrorPtr(new IoSocketError(-read_param_->result_), IoSocketError::deleteIoError)};
        }

        if (read_param_->pending_read_buf_.length() == 0) {
          return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                                    IoSocketError::deleteIoError)};
        }

        const uint64_t max_read_length = std::min(max_length, static_cast<uint64_t>(read_param_->result_));
        uint64_t num_bytes_to_read = read_param_->pending_read_buf_.copyOutToSlices(max_read_length, slices, num_slice);
        read_param_->pending_read_buf_.drain(num_bytes_to_read);
        return {num_bytes_to_read, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
      }
    }
    case IoUringSocketType::Listen:
      break;
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "read, fd = {}, socket type = {}", fd_, ioUringSocketTypeStr());

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client:
      return shadow_io_handle_->read(buffer, max_length_opt);
    case IoUringSocketType::Server: {
      if (!enable_server_socket_) {
        return shadow_io_handle_->read(buffer, max_length_opt);
      } else {
        const uint64_t max_length = max_length_opt.value_or(UINT64_MAX);
        if (max_length == 0) {
          return Api::ioCallUint64ResultNoError();
        }

        Buffer::Reservation reservation = buffer.reserveForRead();
        Api::IoCallUint64Result result = readv(std::min(reservation.length(), max_length),
                                              reservation.slices(), reservation.numSlices());
        uint64_t bytes_to_commit = result.ok() ? result.return_value_ : 0;
        ASSERT(bytes_to_commit <= max_length);
        reservation.commit(bytes_to_commit);
        return result;
      }
    }
    case IoUringSocketType::Listen:
      break;
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "writev, fd = {}", fd_);

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client:
      return shadow_io_handle_->writev(slices, num_slice);
    case IoUringSocketType::Server:
      if (!enable_server_socket_) {
        return shadow_io_handle_->writev(slices, num_slice);
      } else {
        ENVOY_LOG(trace, "server socket write, fd = {}", fd_);
        if (write_param_ != absl::nullopt) {
          // EAGAIN means an injected event, then just submit new write.
          if (write_param_->result_ < 0 && write_param_->result_ != -EAGAIN) {
            return {0, Api::IoErrorPtr(new IoSocketError(write_param_->result_), IoSocketError::deleteIoError)};
          }
          ENVOY_LOG(trace, "an inject event, result = {}, fd = {}", write_param_->result_, fd_);
        }

        ASSERT(io_uring_worker_.has_value());
        auto& io_uring_server_socket = io_uring_worker_.ref().getIoUringSocket(fd_);
        auto ret = io_uring_server_socket.writev(slices, num_slice);
        return {ret, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
      }
    case IoUringSocketType::Listen:
      break;
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "write, length = {}, fd = {}", buffer.length(), fd_);

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client:
      return shadow_io_handle_->write(buffer);
    case IoUringSocketType::Server:
      if (!enable_server_socket_) {
        return shadow_io_handle_->write(buffer);
      } else {
        ENVOY_LOG(trace, "server socket write, fd = {}", fd_);

        if (write_param_ != absl::nullopt) {
          // EAGAIN means an injected event, then just submit new write.
          if (write_param_->result_ < 0 && write_param_->result_ != -EAGAIN) {
            return {
              0, Api::IoErrorPtr(new IoSocketError(write_param_->result_), IoSocketError::deleteIoError)};
          }
          ENVOY_LOG(trace, "an inject event, result = {}, fd = {}", write_param_->result_, fd_);
        }

        ASSERT(io_uring_worker_.has_value());
        auto& io_uring_server_socket = io_uring_worker_.ref().getIoUringSocket(fd_);
        auto ret = io_uring_server_socket.write(buffer);
        if (ret == 0) {
          return {
            0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(), IoSocketError::deleteIoError)};
        }
        return {ret, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
      }
    case IoUringSocketType::Listen:
      break;
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::IoCallUint64Result
IoUringSocketHandleImpl::sendmsg(const Buffer::RawSlice* /*slices*/, uint64_t /*num_slice*/,
                                 int /*flags*/, const Address::Ip* /*self_ip*/,
                                 const Address::Instance& /*peer_address*/) {
  PANIC("not implemented");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmsg(Buffer::RawSlice* /*slices*/,
                                                         const uint64_t /*num_slice*/,
                                                         uint32_t /*self_port*/,
                                                         RecvMsgOutput& /*output*/) {
  PANIC("not implemented");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmmsg(RawSliceArrays& /*slices*/,
                                                          uint32_t /*self_port*/,
                                                          RecvMsgOutput& /*output*/) {
  PANIC("not implemented");
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recv(void* /*buffer*/, size_t /*length*/,
                                                      int /*flags*/) {
  PANIC("not implemented");
}

bool IoUringSocketHandleImpl::supportsMmsg() const { PANIC("not implemented"); }

bool IoUringSocketHandleImpl::supportsUdpGro() const { PANIC("not implemented"); }

Api::SysCallIntResult IoUringSocketHandleImpl::bind(Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "bind to address {}", address->asString());
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoUringSocketHandleImpl::listen(int backlog) {
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Unknown);
  io_uring_socket_type_ = IoUringSocketType::Listen;
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  if (!accepted_socket_param_.has_value()) {
    return nullptr;
  }

  ENVOY_LOG(debug, "IoUringSocketHandleImpl accept the socket, connect fd = {}, remote address = {}",
    accepted_socket_param_->fd_,
    Network::Address::addressFromSockAddrOrThrow(*accepted_socket_param_->remote_addr_,
                                                  accepted_socket_param_->remote_addr_len_, false)->asString());
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Listen);

  //*addr = *reinterpret_cast<struct sockaddr*>(accepted_socket_param_->remote_addr_);
  memcpy(reinterpret_cast<void *>(addr), reinterpret_cast<void *>(accepted_socket_param_->remote_addr_), accepted_socket_param_->remote_addr_len_);
  *addrlen = accepted_socket_param_->remote_addr_len_;
  bool enable_server_socket = true;
  auto io_handle = std::make_unique<IoUringSocketHandleImpl>(read_buffer_size_, io_uring_factory_,
                                                             accepted_socket_param_->fd_, socket_v6only_,
                                                             domain_, enable_server_socket);
  accepted_socket_param_ = absl::nullopt;

  return io_handle;
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  if (io_uring_socket_type_ == IoUringSocketType::Client) {
    return shadow_io_handle_->connect(address);
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::SysCallIntResult IoUringSocketHandleImpl::setOption(int level, int optname, const void* optval,
                                                         socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoUringSocketHandleImpl::getOption(int level, int optname, void* optval,
                                                         socklen_t* optlen) {
  return Api::OsSysCallsSingleton::get().getsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoUringSocketHandleImpl::ioctl(unsigned long, void*, unsigned long, void*,
                                                     unsigned long, unsigned long*) {
  PANIC("not implemented");
}

Api::SysCallIntResult IoUringSocketHandleImpl::setBlocking(bool /*blocking*/) {
  PANIC("not implemented");
}

absl::optional<int> IoUringSocketHandleImpl::domain() { return domain_; }

Address::InstanceConstSharedPtr IoUringSocketHandleImpl::localAddress() {
  // TODO(rojkov): This is a copy-paste from Network::IoSocketHandleImpl.
  // Unification is needed.
  sockaddr_storage ss;
  socklen_t ss_len = sizeof(ss);
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.return_value_ != 0) {
    throw EnvoyException(fmt::format("getsockname failed for '{}': ({}) {}", fd_, result.errno_,
                                     errorDetails(result.errno_)));
  }
  return Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

Address::InstanceConstSharedPtr IoUringSocketHandleImpl::peerAddress() {
  // TODO(rojkov): This is a copy-paste from Network::IoSocketHandleImpl.
  // Unification is needed.
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  Api::SysCallIntResult result =
      os_sys_calls.getpeername(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
  if (result.return_value_ != 0) {
    throw EnvoyException(
        fmt::format("getpeername failed for '{}': {}", errorDetails(result.errno_)));
  }

  if (ss_len == udsAddressLength() && ss.ss_family == AF_UNIX) {
    // For Unix domain sockets, can't find out the peer name, but it should match our own
    // name for the socket (i.e. the path should match, barring any namespace or other
    // mechanisms to hide things, of which there are many).
    ss_len = sizeof ss;
    result = os_sys_calls.getsockname(fd_, reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (result.return_value_ != 0) {
      throw EnvoyException(
          fmt::format("getsockname failed for '{}': {}", fd_, errorDetails(result.errno_)));
    }
  }
  return Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType trigger, uint32_t events) {
  ENVOY_LOG(trace, "initialize file event fd = {}", fd_);
  io_uring_worker_ = io_uring_factory_.getIoUringWorker().ref();

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Server: {
      ENVOY_LOG(trace, "initialize file event for server socket, fd = {}", fd_);
      if (enable_server_socket_) {
        io_uring_worker_->addServerSocket(fd_, *this, read_buffer_size_);
        break;
      } else {
        ENVOY_LOG(trace, "fallback to IoSocketHandle for server socket");
        int flags = fcntl(fd_, F_GETFL, 0);
        ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        shadow_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd_, socket_v6only_, domain_);
        shadow_io_handle_->initializeFileEvent(dispatcher, cb, trigger, events);
        return;
      }
    }
    case IoUringSocketType::Listen: {
      ENVOY_LOG(trace, "initialize file event for accept socket, fd = {}", fd_);
      io_uring_worker_.ref().addAcceptSocket(fd_, *this);
      break;
    }
    case IoUringSocketType::Client:
    case IoUringSocketType::Unknown:
      ENVOY_LOG(trace, "initialize file event for client socket, fd = {}", fd_);
      ENVOY_LOG(trace, "fallback to IoSocketHandle for client socket");
      io_uring_socket_type_ = IoUringSocketType::Client;
      int flags = fcntl(fd_, F_GETFL, 0);
      ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
      shadow_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd_, socket_v6only_, domain_);
      shadow_io_handle_->initializeFileEvent(dispatcher, cb, trigger, events);
      return;
  }

  cb_ = std::move(cb);
}

IoHandlePtr IoUringSocketHandleImpl::duplicate() { PANIC("not implemented"); }

void IoUringSocketHandleImpl::activateFileEvents(uint32_t events) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "activate file events {}, fd = {}, io_uring_socket_type = {}", events, fd_, ioUringSocketTypeStr());

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client: {
      shadow_io_handle_->activateFileEvents(events);
      return;
    }
    case IoUringSocketType::Server: {
      if (!enable_server_socket_) {
        ASSERT(shadow_io_handle_ != nullptr);
        shadow_io_handle_->activateFileEvents(events);
        return;
      }
    }
    case IoUringSocketType::Listen: {
      // TODO (soulxu): maybe not use EAGAIN here.
      if (events & Event::FileReadyType::Read) {
        ENVOY_LOG(trace, "inject read event, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());
        io_uring_worker_.ref().injectCompletion(fd_, Io::RequestType::Read, -EAGAIN);
      }
      if (events & Event::FileReadyType::Write) {
        ENVOY_LOG(trace, "inject write event, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());
        io_uring_worker_.ref().injectCompletion(fd_, Io::RequestType::Write, -EAGAIN);
      }
      return;
    }
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

void IoUringSocketHandleImpl::enableFileEvents(uint32_t events) {
  ENVOY_LOG(trace, "enable file events {}, fd = {}, io_uring_socket_type = {}", events, fd_, ioUringSocketTypeStr());
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client: {
      shadow_io_handle_->enableFileEvents(events);
      return;
    }
    case IoUringSocketType::Server: {
      if (!enable_server_socket_) {
        ASSERT(shadow_io_handle_ != nullptr);
        shadow_io_handle_->enableFileEvents(events);
        return;
      }
    }
    case IoUringSocketType::Listen: {
      if (!(events & Event::FileReadyType::Read)) {
        io_uring_worker_.ref().disableSocket(fd_);
      } else {
        io_uring_worker_.ref().enableSocket(fd_);
      }
      return;
    }
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

void IoUringSocketHandleImpl::resetFileEvents() {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  ENVOY_LOG(trace, "reset file, fd = {}, io_uring_socket_type = {}", fd_, ioUringSocketTypeStr());

  switch (io_uring_socket_type_) {
    case IoUringSocketType::Client: {
      shadow_io_handle_->resetFileEvents();
      return;
    }
    case IoUringSocketType::Server: {
      if (!enable_server_socket_) {
        ASSERT(shadow_io_handle_ != nullptr);
        shadow_io_handle_->resetFileEvents();
        return;
      }
    }
    case IoUringSocketType::Listen: {
      io_uring_worker_.ref().disableSocket(fd_);
      return;
    }
    case IoUringSocketType::Unknown:
      break;
  }

  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int how) {
  return Api::OsSysCallsSingleton::get().shutdown(fd_, how);
}

absl::optional<std::string> IoUringSocketHandleImpl::interfaceName() {
  // TODO(rojkov): This is a copy-paste from Network::IoSocketHandleImpl.
  // Unification is needed.
  auto& os_syscalls_singleton = Api::OsSysCallsSingleton::get();
  if (!os_syscalls_singleton.supportsGetifaddrs()) {
    return absl::nullopt;
  }

  Address::InstanceConstSharedPtr socket_address = localAddress();
  if (!socket_address || socket_address->type() != Address::Type::Ip) {
    return absl::nullopt;
  }

  Api::InterfaceAddressVector interface_addresses{};
  const Api::SysCallIntResult rc = os_syscalls_singleton.getifaddrs(interface_addresses);
  RELEASE_ASSERT(!rc.return_value_, fmt::format("getiffaddrs error: {}", rc.errno_));

  absl::optional<std::string> selected_interface_name{};
  for (const auto& interface_address : interface_addresses) {
    if (!interface_address.interface_addr_) {
      continue;
    }

    if (socket_address->ip()->version() == interface_address.interface_addr_->ip()->version()) {
      // Compare address _without port_.
      // TODO: create common addressAsStringWithoutPort method to simplify code here.
      absl::uint128 socket_address_value;
      absl::uint128 interface_address_value;
      switch (socket_address->ip()->version()) {
      case Address::IpVersion::v4:
        socket_address_value = socket_address->ip()->ipv4()->address();
        interface_address_value = interface_address.interface_addr_->ip()->ipv4()->address();
        break;
      case Address::IpVersion::v6:
        socket_address_value = socket_address->ip()->ipv6()->address();
        interface_address_value = interface_address.interface_addr_->ip()->ipv6()->address();
        break;
      default:
        ENVOY_BUG(false, fmt::format("unexpected IP family {}",
                                     static_cast<int>(socket_address->ip()->version())));
      }

      if (socket_address_value == interface_address_value) {
        selected_interface_name = interface_address.interface_name_;
        break;
      }
    }
  }

  return selected_interface_name;
}

void IoUringSocketHandleImpl::onAcceptSocket(Io::AcceptedSocketParam& param) {
  accepted_socket_param_ = param;
  ENVOY_LOG(trace, "before on accept socket");
  cb_(Event::FileReadyType::Read);
  ENVOY_LOG(trace, "after on accept socket");

  // After accept the socet, the accepted_socket_param expected to be cleanup.
  ASSERT(accepted_socket_param_ == absl::nullopt);
}

void IoUringSocketHandleImpl::onRead(Io::ReadParam& param) {
  read_param_ = param;
  if (read_param_->result_ > 0) {
    while (read_param_->pending_read_buf_.length() > 0) {
      ENVOY_LOG(trace, "calling event callback since pending read buf has {} size data, data = {}, io_uring_socket_type = {}", read_param_->pending_read_buf_.length(), read_param_->pending_read_buf_.toString(), ioUringSocketTypeStr());
      cb_(Event::FileReadyType::Read);
    }
  } else {
    ENVOY_LOG(trace, "call event callback since result = {}, io_uring_socket_type = {}", read_param_->result_, ioUringSocketTypeStr());
    cb_(Event::FileReadyType::Read);
  }
  read_param_ = absl::nullopt;
}

void IoUringSocketHandleImpl::onWrite(Io::WriteParam& param) {
  write_param_ = param;
  ENVOY_LOG(trace, "call event callback for write since result = {}", write_param_->result_);
  cb_(Event::FileReadyType::Write);
  write_param_ = absl::nullopt;
}

} // namespace Network
} // namespace Envoy
