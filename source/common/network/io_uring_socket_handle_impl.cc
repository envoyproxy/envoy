#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "io_uring_socket_handle_impl.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/io/io_uring.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"

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
  if (is_server_socket) {
    io_uring_socket_type_ = IoUringSocketType::Server;
    io_uring_worker_ = io_uring_factory_.getIoUringWorker().ref();
    io_uring_worker_->addServerSocket(fd_, *this, read_buffer_size_, true);
  }
}

IoUringSocketHandleImpl::~IoUringSocketHandleImpl() {
  if (SOCKET_VALID(fd_)) {
    // The TLS slot has been shut down by this moment with IoUring wiped out, thus
    // better use this posix system call instead of IoUringSocketHandleImpl::close().
    ::close(fd_);
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  // There is case the socket will be closed directly without initialize any event.
  if (io_uring_socket_type_ == IoUringSocketType::Unknown) {
    ::close(fd_);
    SET_SOCKET_INVALID(fd_);
    return Api::ioCallUint64ResultNoError();
  }

  if (io_uring_socket_type_ == IoUringSocketType::Listen || io_uring_socket_type_ == IoUringSocketType::Server) {
   io_uring_worker_.ref().closeSocket(fd_);
   SET_SOCKET_INVALID(fd_);
   return Api::ioCallUint64ResultNoError();
  }

  auto& uring = io_uring_factory_.get().ref();
  if (read_req_) {
    auto req = new Io::Request{*this, Io::RequestType::Cancel};
    auto res = uring.prepareCancel(read_req_, req);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      uring.submit();
      res = uring.prepareCancel(read_req_, req);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
    }
  }

  auto req = new Io::Request{absl::nullopt, Io::RequestType::Close};
  auto res = uring.prepareClose(fd_, req);
  if (res == Io::IoUringResult::Failed) {
    // Fall back to posix system call.
    ::close(fd_);
  }
  uring.submit();
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

bool IoUringSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }

Api::IoCallUint64Result
IoUringSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  if (io_uring_socket_type_ == IoUringSocketType::Server) {
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

  if (remote_closed_) {
    return Api::ioCallUint64ResultNoError();
  }

  if (bytes_to_read_ < 0) {
    return {0, Api::IoErrorPtr(new IoSocketError(-bytes_to_read_), IoSocketError::deleteIoError)};
  }

  if (bytes_to_read_ == 0 || read_req_ == nullptr) {
    addReadRequest();
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  const uint64_t max_read_length = std::min(max_length, static_cast<uint64_t>(bytes_to_read_));
  uint64_t num_bytes_to_read = read_buf_.copyOutToSlices(max_read_length, slices, num_slice);
  ASSERT(num_bytes_to_read <= max_read_length);
  read_buf_.drain(num_bytes_to_read);
  bytes_to_read_ -= num_bytes_to_read;
  if (bytes_to_read_ == 0) {
    bytes_to_read_ = 0;
    read_req_ = nullptr;
    addReadRequest();
  }

  return {num_bytes_to_read, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
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

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  if (is_write_added_) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  if (bytes_already_wrote_ < 0) {
    return {
        0, Api::IoErrorPtr(new IoSocketError(-bytes_already_wrote_), IoSocketError::deleteIoError)};
  }

  if (bytes_already_wrote_ > 0) {
    uint64_t len = bytes_already_wrote_;
    bytes_already_wrote_ = 0;
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
    auto req = new Io::Request{*this, Io::RequestType::Write, iovecs};
    auto& uring = io_uring_factory_.get().ref();
    auto res = uring.prepareWritev(fd_, iovecs, num_slice, 0, req);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      uring.submit();
      res = uring.prepareWritev(fd_, iovecs, num_slice, 0, req);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
    }
    // Need to ensure the write request submitted.
    uring.submit();
  }

  return {
      0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(), IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  // If buffer gets written and drained, the following writev will return bytes_already_wrote_
  // directly.
  if (bytes_already_wrote_ > 0) {
    buffer.drain(static_cast<uint64_t>(bytes_already_wrote_));
  }

  Buffer::RawSliceVector slices = buffer.getRawSlices();
  return writev(slices.begin(), slices.size());
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
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Unknown);
  io_uring_socket_type_ = IoUringSocketType::Listen;
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  if (!accepted_socket_param_.has_value()) {
    return nullptr;
  }

  ENVOY_LOG(debug, "IoUringSocketHandleImpl accept the socket");
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Listen);

  *addr = accepted_socket_param_->remote_addr_;
  *addrlen = accepted_socket_param_->remote_addr_len_;
  bool enable_server_socket = false;
  auto io_handle = std::make_unique<IoUringSocketHandleImpl>(read_buffer_size_, io_uring_factory_,
                                                             accepted_socket_param_->fd_, socket_v6only_,
                                                             domain_, enable_server_socket);
  if (!enable_server_socket) {
    io_handle->addReadRequest();
  }
  accepted_socket_param_ = absl::nullopt;

  return io_handle;
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  auto& uring = io_uring_factory_.get().ref();
  auto req = new Io::Request{*this, Io::RequestType::Connect};
  auto res = uring.prepareConnect(fd_, address, req);
  if (res == Io::IoUringResult::Failed) {
    res = uring.submit();
    if (res == Io::IoUringResult::Busy) {
      return Api::SysCallIntResult{0, SOCKET_ERROR_AGAIN};
    }
    res = uring.prepareConnect(fd_, address, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare connect");
  }
  // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
  uring.submit();
  return Api::SysCallIntResult{0, SOCKET_ERROR_IN_PROGRESS};
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

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher&,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType, uint32_t) {
  io_uring_worker_ = io_uring_factory_.getIoUringWorker().ref();

  if (io_uring_socket_type_ == IoUringSocketType::Listen) {
    //addAcceptRequest();
    //io_uring_factory_.get().ref().submit();
    io_uring_worker_.ref().addAcceptSocket(fd_, *this);
  } else if (io_uring_socket_type_ == IoUringSocketType::Server) {
    io_uring_worker_.ref().enableSocket(fd_);
  } else {
    ASSERT(io_uring_socket_type_ == IoUringSocketType::Unknown);
    io_uring_socket_type_ = IoUringSocketType::Client;
  }

  cb_ = std::move(cb);
}

IoHandlePtr IoUringSocketHandleImpl::duplicate() { PANIC("not implemented"); }

void IoUringSocketHandleImpl::activateFileEvents(uint32_t events) {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  if ((io_uring_socket_type_ == IoUringSocketType::Listen || io_uring_socket_type_ == IoUringSocketType::Server) && (events & Event::FileReadyType::Read)) {
    if (events & Event::FileReadyType::Read) {
      io_uring_worker_.ref().injectCompletion(fd_, Io::RequestType::Read, -EAGAIN);
    }
    if (events & Event::FileReadyType::Write) {
      // go to the old path until server socket enable write.
      //io_uring_worker_.ref().injectCompletion(fd_, Io::RequestType::Write);
    }
  }

  // old code path.
  if (events & Event::FileReadyType::Write) {
    if (io_uring_socket_type_ != IoUringSocketType::Server) {
      addReadRequest();
    }
    cb_(Event::FileReadyType::Write);
  }
}

void IoUringSocketHandleImpl::enableFileEvents(uint32_t events) {
  ENVOY_LOG(trace, "enable file events {}, fd = {}, io_uring_socket_type = {}", events, fd_, ioUringSocketTypeStr());
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);

  if (io_uring_socket_type_ == IoUringSocketType::Listen || io_uring_socket_type_ == IoUringSocketType::Server) {
    if (!(events & Event::FileReadyType::Read)) {
      io_uring_worker_.ref().disableSocket(fd_);
    } else {
      io_uring_worker_.ref().enableSocket(fd_);
    }
    return;
  }

  // old code path.
  if (events & Event::FileReadyType::Read) {
    is_read_enabled_ = true;
    addReadRequest();
    cb_(Event::FileReadyType::Read);
  } else {
    is_read_enabled_ = false;
  }
}

void IoUringSocketHandleImpl::resetFileEvents() {
  ASSERT(io_uring_socket_type_ != IoUringSocketType::Unknown);
  io_uring_worker_.ref().disableSocket(fd_);
}

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int how) {
  return Api::OsSysCallsSingleton::get().shutdown(fd_, how);
}

void IoUringSocketHandleImpl::addReadRequest() {
  if (!is_read_enabled_ || !SOCKET_VALID(fd_) || read_req_) {
    return;
  }

  read_req_ = new Io::Request{*this, Io::RequestType::Read};
  read_req_->buf_ = std::make_unique<uint8_t[]>(read_buffer_size_);
  read_req_->iov_ = new struct iovec[1];
  read_req_->iov_->iov_base = read_req_->buf_.get();
  read_req_->iov_->iov_len = read_buffer_size_;
  auto& uring = io_uring_factory_.get().ref();
  auto res = uring.prepareReadv(fd_, read_req_->iov_, 1, 0, read_req_);

  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
    res = uring.prepareReadv(fd_, read_req_->iov_, 1, 0, read_req_);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
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
  cb_(Event::FileReadyType::Read);

  // After accept the socet, the accepted_socket_param expected to be cleanup.
  ASSERT(accepted_socket_param_ == absl::nullopt);
}

void IoUringSocketHandleImpl::onRead(Io::ReadParam& param) {
  read_param_ = param;
  if (read_param_->result_ > 0) {
    while (read_param_->pending_read_buf_.length() > 0) {
      ENVOY_LOG(trace, "calling event callback since pending read buf has {} size data, data = {}", read_param_->pending_read_buf_.length(), read_param_->pending_read_buf_.toString());
      cb_(Event::FileReadyType::Read);
    }
  } else {
    ENVOY_LOG(trace, "call event callback since result = {}", read_param_->result_);
    cb_(Event::FileReadyType::Read);
  }
  read_param_ = absl::nullopt;
}

void IoUringSocketHandleImpl::onRequestCompletion(const Io::Request& req,
                                                  int32_t result) {
  if (result < 0) {
    ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
  }

  // This is hacky fix, we should check the req is valid or not.
  if (fd_ == -1) {
    ENVOY_LOG_MISC(debug, "the uring's fd already closed");
    return;
  }

  switch (req.type_) {
  case Io::RequestType::Accept:
    // All the logic moved to IoUring worker, suppose
    // not reach here anymore.
    PANIC("not impelement");
    break;
  case Io::RequestType::Read: {
    // Read is cancellable.
    if (result == -ECANCELED) {
      return;
    }

    // This is hacky fix, we should check the req is valid or not.
    if (fd_ == -1) {
      ENVOY_LOG_MISC(debug, "the uring's fd already closed");
      return;
    }

    bytes_to_read_ = result;
    if (result == 0) {
      remote_closed_ = true;
    }
    if (result > 0) {
      Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
          const_cast<Io::Request&>(req).buf_.release(), result,
          [](const void* data, size_t /*len*/, const Buffer::BufferFragmentImpl* this_fragment) {
            delete[] reinterpret_cast<const uint8_t*>(data);
            delete this_fragment;
          });
      read_buf_.addBufferFragment(*fragment);
    }
    cb_(Event::FileReadyType::Read);
    break;
  }
  case Io::RequestType::Connect: {
    if (result < 0) {
      cb_(Event::FileReadyType::Closed);
      return;
    }

    cb_(Event::FileReadyType::Write);
    addReadRequest();
    break;
  }
  case Io::RequestType::Write: {
    // This is hacky fix, we should check the req is valid or not.
    if (fd_ == -1) {
      ENVOY_LOG_MISC(debug, "the uring's fd already closed");
      return;
    }

    bytes_already_wrote_ = result;
    is_write_added_ = false;
    cb_(Event::FileReadyType::Write);
    break;
  }
  case Io::RequestType::Close:
    break;
  case Io::RequestType::Cancel:
    break;
  default:
    PANIC("not implemented");
  }
}

} // namespace Network
} // namespace Envoy
