#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/io/io_uring.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

namespace {

constexpr socklen_t udsAddressLength() { return sizeof(sa_family_t); }

} // namespace

IoUringSocketHandleImpl::IoUringSocketHandleImpl(const uint32_t read_buffer_size,
                                                 const Io::IoUringFactory& io_uring_factory,
                                                 os_fd_t fd, bool socket_v6only,
                                                 absl::optional<int> domain)
    : read_buffer_size_(read_buffer_size), io_uring_factory_(io_uring_factory), fd_(fd),
      socket_v6only_(socket_v6only), domain_(domain) {}

IoUringSocketHandleImpl::~IoUringSocketHandleImpl() {
  if (SOCKET_VALID(fd_)) {
    // The TLS slot has been shut down by this moment with IoUring wiped out, thus
    // better use this posix system call instead of IoUringSocketHandleImpl::close().
    ::close(fd_);
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  if (read_req_) {
    auto req = new Request{RequestType::Cancel};
    auto res = ioUring().prepareCancel(read_req_, req, nullptr);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      ioUring().submit();
      res = ioUring().prepareCancel(read_req_, req, nullptr);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
    }
  }
  if (accept_req_) {
    auto req = new Request{RequestType::Cancel};
    auto res = ioUring().prepareCancel(accept_req_, req, nullptr);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      ioUring().submit();
      res = ioUring().prepareCancel(accept_req_, req, nullptr);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
    }
  }

  auto req = new Request{RequestType::Close};
  auto res = ioUring().prepareClose(fd_, req, nullptr);
  if (res == Io::IoUringResult::Failed) {
    // Fall back to posix system call.
    ::close(fd_);
  }
  ioUring().trySubmit();
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

bool IoUringSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }

Api::IoCallUint64Result
IoUringSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice) {
  if (remote_closed_) {
    return Api::ioCallUint64ResultNoError();
  }

  if (read_ret_ < 0) {
    return {0, Api::IoErrorPtr(new IoSocketError(-read_ret_), IoSocketError::deleteIoError)};
  }

  if (read_ret_ == 0 || read_req_ == nullptr) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  const uint64_t max_read_length = std::min(max_length, static_cast<uint64_t>(read_ret_));
  uint64_t num_bytes_to_read = read_buf_.copyOutToSlices(max_read_length, slices, num_slice);
  ASSERT(num_bytes_to_read <= max_read_length);
  read_buf_.drain(num_bytes_to_read);
  read_ret_ -= num_bytes_to_read;
  if (read_ret_ == 0) {
    read_ret_ = 0;
    read_req_ = nullptr;
    addReadRequest();
  }

  return {num_bytes_to_read, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  const uint64_t max_length = max_length_opt.value_or(UINT64_MAX);
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  if (remote_closed_) {
    return Api::ioCallUint64ResultNoError();
  }

  if (read_ret_ < 0) {
    return {0, Api::IoErrorPtr(new IoSocketError(-read_ret_), IoSocketError::deleteIoError)};
  }

  if (read_ret_ == 0 || read_req_ == nullptr) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  uint64_t num_bytes_to_read = buffer.length();
  buffer.move(read_buf_, max_length);
  num_bytes_to_read = buffer.length() - num_bytes_to_read;
  ASSERT(num_bytes_to_read <= max_length);
  read_ret_ -= num_bytes_to_read;
  if (read_ret_ == 0) {
    read_ret_ = 0;
    read_req_ = nullptr;
    addReadRequest();
  }

  return {num_bytes_to_read, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  if (is_write_added_) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  if (write_ret_ < 0) {
    return {0, Api::IoErrorPtr(new IoSocketError(-write_ret_), IoSocketError::deleteIoError)};
  }

  if (write_ret_ > 0) {
    uint64_t len = write_ret_;
    write_ret_ = 0;
    return {len, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
  }

  struct iovec* iovecs = new struct iovec[num_slice];
  struct iovec* iov = iovecs;
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; ++i) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }

  if (num_slices_to_write > 0) {
    is_write_added_ = true; // don't add WRITE if it's been already added.
    auto req = new Request{RequestType::Write, iovecs};
    auto res = ioUring().prepareWritev(
        fd_, iovecs, num_slice, 0, req, [this](void* user_data, int32_t result) {
          this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
        });
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      ioUring().submit();
      res = ioUring().prepareWritev(
          fd_, iovecs, num_slice, 0, req, [this](void* user_data, int32_t result) {
            this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
          });
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
    }
    // Need to ensure the write request submitted.
    ioUring().trySubmit();
  }

  return {
      0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(), IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  constexpr uint64_t MaxSlices = 16;
  Buffer::RawSliceVector slices = buffer.getRawSlices(MaxSlices);
  auto result = writev(slices.begin(), slices.size());
  if (result.return_value_ > 0) {
    buffer.drain(static_cast<uint64_t>(result.return_value_));
  }
  return result;
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
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoUringSocketHandleImpl::listen(int backlog) {
  is_listener_ = true;
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  if (accept_req_ == nullptr || SOCKET_INVALID(connection_fd_)) {
    return nullptr;
  }

  *addr = connection_addr_;
  *addrlen = connection_addr_len_;
  auto io_handle = std::make_unique<IoUringSocketHandleImpl>(read_buffer_size_, io_uring_factory_,
                                                             connection_fd_);
  io_handle->addReadRequest();
  SET_SOCKET_INVALID(connection_fd_);
  accept_req_ = nullptr;
  addAcceptRequest();
  return io_handle;
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  auto req = new Request{RequestType::Connect};
  auto res = ioUring().prepareConnect(fd_, address, req, [this](void* user_data, int32_t result) {
    this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
  });
  if (res == Io::IoUringResult::Failed) {
    res = ioUring().submit();
    if (res == Io::IoUringResult::Busy) {
      return Api::SysCallIntResult{0, SOCKET_ERROR_AGAIN};
    }
    res = ioUring().prepareConnect(fd_, address, req, [this](void* user_data, int32_t result) {
      this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
    });
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare connect");
  }
  // Need to ensure the connect request submitted.
  ioUring().trySubmit();
  return Api::SysCallIntResult{0, SOCKET_ERROR_IN_PROGRESS};
}

Api::SysCallIntResult IoUringSocketHandleImpl::setOption(int level, int optname, const void* optval,
                                                         socklen_t optlen) {
  return Api::OsSysCallsSingleton::get().setsockopt(fd_, level, optname, optval, optlen);
}

Api::SysCallIntResult IoUringSocketHandleImpl::getOption(int level, int optname, void* optval,
                                                         socklen_t* optlen) {
  // ConnectionImpl will check connect result via getOption.
  if (connect_ret_ < 0 && optname == SO_ERROR) {
    int ret = connect_ret_;
    connect_ret_ = 1;
    return Api::SysCallIntResult{0, -ret};
  }

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

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher&, Event::FileReadyCb cb,
                                                  Event::FileTriggerType, uint32_t) {
  cb_ = std::move(cb);
  if (is_listener_) {
    addAcceptRequest();
    ioUring().trySubmit();
  }
}

IoHandlePtr IoUringSocketHandleImpl::duplicate() { PANIC("not implemented"); }

void IoUringSocketHandleImpl::activateFileEvents(uint32_t events) {
  if (events & Event::FileReadyType::Write) {
    addReadRequest();
    cb_(Event::FileReadyType::Write);
  }
}

void IoUringSocketHandleImpl::enableFileEvents(uint32_t events) {
  if (events & Event::FileReadyType::Read) {
    is_read_enabled_ = true;
    addReadRequest();
    auto req = new Request{RequestType::Unknown};
    auto res = ioUring().prepareNop(
        req, [this](void*, int32_t) { this->cb_(Event::FileReadyType::Read); });
    if (res == Io::IoUringResult::Failed) {
      res = ioUring().submit();
      res = ioUring().prepareNop(req,
                                 [this](void*, int32_t) { this->cb_(Event::FileReadyType::Read); });
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare nop");
    }
  } else {
    is_read_enabled_ = false;
  }
}

void IoUringSocketHandleImpl::resetFileEvents() {}

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

Io::IoUring& IoUringSocketHandleImpl::ioUring() {
  if (io_uring_ == absl::nullopt) {
    io_uring_ = io_uring_factory_.get();
  }

  return io_uring_.ref();
}

void IoUringSocketHandleImpl::addAcceptRequest() {
  if (accept_req_) {
    return;
  }

  accept_req_ = new Request{RequestType::Accept};
  auto res = ioUring().prepareAccept(
      fd_, &accept_req_->remote_addr_, &accept_req_->remote_addr_len_, accept_req_,
      [this](void* user_data, int32_t result) {
        this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
      });
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    ioUring().submit();
    res = ioUring().prepareAccept(fd_, &accept_req_->remote_addr_, &accept_req_->remote_addr_len_,
                                  accept_req_, [this](void* user_data, int32_t result) {
                                    this->onRequestCompletion(reinterpret_cast<Request*>(user_data),
                                                              result);
                                  });
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
}

void IoUringSocketHandleImpl::addReadRequest() {
  if (!is_read_enabled_ || SOCKET_INVALID(fd_) || read_req_) {
    return;
  }

  read_req_ = new Request{RequestType::Read};
  read_req_->buf_ = std::make_unique<uint8_t[]>(read_buffer_size_);
  read_req_->iov_ = new struct iovec[1];
  read_req_->iov_->iov_base = read_req_->buf_.get();
  read_req_->iov_->iov_len = read_buffer_size_;
  auto res = ioUring().prepareReadv(
      fd_, read_req_->iov_, 1, 0, read_req_, [this](void* user_data, int32_t result) {
        this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
      });
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    ioUring().submit();
    res = ioUring().prepareReadv(
        fd_, read_req_->iov_, 1, 0, read_req_, [this](void* user_data, int32_t result) {
          this->onRequestCompletion(reinterpret_cast<Request*>(user_data), result);
        });
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
}

void IoUringSocketHandleImpl::onRequestCompletion(Request* request, int32_t result) {
  if (result < 0) {
    ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
  }
  // TODO(zhxie): Cancel requests instead of escaping completion.
  if (SOCKET_VALID(fd_)) {
    switch (request->type_) {
    case RequestType::Accept:
      ASSERT(SOCKET_INVALID(connection_fd_));
      connection_fd_ = result;
      connection_addr_ = request->remote_addr_;
      connection_addr_len_ = request->remote_addr_len_;
      cb_(Event::FileReadyType::Read);
      break;
    case RequestType::Connect:
      connect_ret_ = result;
      cb_(Event::FileReadyType::Write);
      if (result >= 0) {
        addReadRequest();
      }
      break;
    case RequestType::Read:
      read_ret_ = result;
      if (result == 0) {
        remote_closed_ = true;
      } else if (result > 0) {
        Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
            request->buf_.release(), result,
            [](const void* data, size_t /*len*/, const Buffer::BufferFragmentImpl* this_fragment) {
              delete[] reinterpret_cast<const uint8_t*>(data);
              delete this_fragment;
            });
        read_buf_.addBufferFragment(*fragment);
      }
      cb_(Event::FileReadyType::Read);
      break;
    case RequestType::Write:
      write_ret_ = result;
      is_write_added_ = false;
      cb_(Event::FileReadyType::Write);
      break;
    case RequestType::Close:
      break;
    case RequestType::Cancel:
      break;
    default:
      PANIC("not implemented");
    }
  }

  // Cleanup.
  if (request->iov_) {
    delete[] request->iov_;
  }
  delete request;
}

} // namespace Network
} // namespace Envoy
