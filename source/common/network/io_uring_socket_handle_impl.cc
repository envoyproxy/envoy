#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
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
  auto req = new Request{absl::nullopt, RequestType::Close};
  Io::IoUringResult res = io_uring_factory_.get().ref().prepareClose(fd_, req);
  if (res == Io::IoUringResult::Failed) {
    // Fall back to posix system call.
    ::close(fd_);
  }
  if (isLeader()) {
    if (io_uring_factory_.get().ref().isEventfdRegistered()) {
      io_uring_factory_.get().ref().unregisterEventfd();
    }
    file_event_adapter_.reset();
  }
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

bool IoUringSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }
Api::IoCallUint64Result IoUringSocketHandleImpl::readv(uint64_t /* max_length */,
                                                       Buffer::RawSlice* slices,
                                                       uint64_t num_slice) {
  if (read_buf_ == nullptr) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (;
       num_slices_to_read < num_slice && num_bytes_to_read < static_cast<uint64_t>(bytes_to_read_);
       num_slices_to_read++) {
    const size_t slice_length = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(bytes_to_read_ - num_bytes_to_read));
    memcpy(slices[num_slices_to_read].mem_, read_buf_.get() + num_bytes_to_read, slice_length);
    num_bytes_to_read += slice_length;
  }
  ASSERT(num_bytes_to_read <= static_cast<uint64_t>(bytes_to_read_));
  is_read_added_ = false;

  uint64_t len = bytes_to_read_;
  bytes_to_read_ = 0;
  return {len, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  const uint64_t max_length = max_length_opt.value_or(UINT64_MAX);
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  if (bytes_to_read_ == 0) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }

  if (read_buf_ == nullptr) {
    return {0, Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                               IoSocketError::deleteIoError)};
  }
  auto fragment = new Buffer::BufferFragmentImpl(
      read_buf_.release(), bytes_to_read_,
      [](const void* data, size_t /*len*/, const Buffer::BufferFragmentImpl* this_fragment) {
        delete[] reinterpret_cast<const uint8_t*>(data);
        delete this_fragment;
      });
  buffer.addBufferFragment(*fragment);
  is_read_added_ = false;

  uint64_t len = bytes_to_read_;
  bytes_to_read_ = 0;
  return {len, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  Buffer::OwnedImpl buffer;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      buffer.add(slices[i].mem_, slices[i].len_);
    }
  }
  return write(buffer);
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  auto length = buffer.length();
  ASSERT(length > 0);

  while (buffer.length() > 0) {
    // The buffer must not own the data after it has been extracted and put into
    // the `io-uring` submission queue to avoid freeing it before the writev
    // operation is completed.
    Buffer::SliceDataPtr data = buffer.extractMutableFrontSlice();
    write_buf_.push_back(std::move(data));
  }

  addWriteRequest();
  // Need to ensure the write request submitted.
  auto& uring = io_uring_factory_.get().ref();
  uring.submit();
  return {length, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError)};
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
  file_event_adapter_ =
      std::make_unique<FileEventAdapter>(read_buffer_size_, io_uring_factory_, fd_);
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  return file_event_adapter_->accept(addr, addrlen);
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  auto& uring = io_uring_factory_.get().ref();
  auto req = new Request{*this, RequestType::Connect};
  auto res = uring.prepareConnect(fd_, address, req);
  if (res == Io::IoUringResult::Failed) {
    res = uring.submit();
    if (res == Io::IoUringResult::Busy) {
      return Api::SysCallIntResult{0, SOCKET_ERROR_AGAIN};
    }
    res = uring.prepareConnect(fd_, address, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare connect");
  }
  if (isLeader()) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
  }
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

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType trigger, uint32_t events) {
  // Check if this is a server socket accepting new connections.
  if (isLeader()) {
    // Multiple listeners in single thread, there can be registered by other listener.
    if (!io_uring_factory_.get().ref().isEventfdRegistered()) {
      file_event_adapter_->initialize(dispatcher, cb, trigger, events);
    }
    file_event_adapter_->addAcceptRequest();
    io_uring_factory_.get().ref().submit();
    return;
  }

  // Check if this is going to become a leading client socket.
  if (!io_uring_factory_.get().ref().isEventfdRegistered()) {
    file_event_adapter_ =
        std::make_unique<FileEventAdapter>(read_buffer_size_, io_uring_factory_, fd_);
    file_event_adapter_->initialize(dispatcher, cb, trigger, events);
  }

  cb_ = std::move(cb);
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
  } else {
    is_read_enabled_ = false;
  }
}

void IoUringSocketHandleImpl::resetFileEvents() { file_event_adapter_.reset(); }

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int /*how*/) { PANIC("not implemented"); }

void IoUringSocketHandleImpl::addReadRequest() {
  if (!is_read_enabled_ || !SOCKET_VALID(fd_) || is_read_added_) {
    return;
  }

  ASSERT(read_buf_ == nullptr);
  is_read_added_ = true; // don't add READ if it's been already added.
  read_buf_ = std::unique_ptr<uint8_t[]>(new uint8_t[read_buffer_size_]);
  iov_.iov_base = read_buf_.get();
  iov_.iov_len = read_buffer_size_;
  auto& uring = io_uring_factory_.get().ref();
  auto req = new Request{*this, RequestType::Read};
  auto res = uring.prepareReadv(fd_, &iov_, 1, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
    res = uring.prepareReadv(fd_, &iov_, 1, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
}

void IoUringSocketHandleImpl::addWriteRequest() {
  if (is_write_added_ || write_buf_.empty()) {
    return;
  }

  is_write_added_ = true; // don't add WRITE if it's been already added.
  uint32_t nr_vecs = write_buf_.size();
  struct iovec* iovecs = new struct iovec[write_buf_.size()];
  struct iovec* iov = iovecs;
  for (auto& slice : write_buf_) {
    absl::Span<uint8_t> mdata = slice->getMutableData();
    iov->iov_base = mdata.data();
    iov->iov_len = mdata.size();
    iov++;
  }

  auto req = new Request{*this, RequestType::Write, iovecs, std::move(write_buf_)};
  write_buf_ = std::list<Buffer::SliceDataPtr>{};
  auto& uring = io_uring_factory_.get().ref();
  auto res = uring.prepareWritev(fd_, iovecs, nr_vecs, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
    res = uring.prepareWritev(fd_, iovecs, nr_vecs, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
  }
  vecs_to_write_ = nr_vecs;
  // Make the IO handle start reading to avoid read timeout in procedures out of Envoy's scope
  // including handshaking of TLS.
  addReadRequest();
}

void IoUringSocketHandleImpl::continueWriting(Request& req, uint32_t offset) {
  auto iovecs = req.iov_;
  while (offset > 0 && vecs_to_write_ > 0) {
    size_t length = iovecs->iov_len;
    // The iovec has been written completly.
    if (offset >= length) {
      iovecs++;
      vecs_to_write_--;
      offset -= length;
      continue;
    }

    // The iovec has been written partially.
    uint8_t* iov_base = reinterpret_cast<uint8_t*>(iovecs->iov_base);
    iovecs->iov_base = iov_base + offset;
    iovecs->iov_len -= offset;
    break;
  }

  // The WRITE has been completed.
  if (!vecs_to_write_) {
    iovecs -= req.slices_.size();
    delete[] iovecs;

    is_write_added_ = false;
    addWriteRequest();
    return;
  }

  // The WRITE is not completed. Resubmit the trimmed request.
  auto new_req = new Request{*this, RequestType::Write, iovecs, std::move(req.slices_)};
  auto& uring = io_uring_factory_.get().ref();
  auto res = uring.prepareWritev(fd_, iovecs, vecs_to_write_, 0, new_req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
    res = uring.prepareWritev(fd_, iovecs, vecs_to_write_, 0, new_req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
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

IoHandlePtr IoUringSocketHandleImpl::FileEventAdapter::accept(struct sockaddr* addr,
                                                              socklen_t* addrlen) {
  if (!is_accept_added_) {
    return nullptr;
  }

  ASSERT(SOCKET_VALID(connection_fd_));

  is_accept_added_ = false;
  *addr = remote_addr_;
  *addrlen = remote_addr_len_;
  auto io_handle = std::make_unique<IoUringSocketHandleImpl>(read_buffer_size_, io_uring_factory_,
                                                             connection_fd_);
  SET_SOCKET_INVALID(connection_fd_);
  io_handle->addReadRequest();
  return io_handle;
}

void IoUringSocketHandleImpl::FileEventAdapter::onRequestCompletion(const Request& req,
                                                                    int32_t result) {
  if (result < 0) {
    ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
  }

  switch (req.type_) {
  case RequestType::Accept:
    ASSERT(!SOCKET_VALID(connection_fd_));
    addAcceptRequest();
    if (result >= 0) {
      connection_fd_ = result;
      cb_(Event::FileReadyType::Read);
    }
    break;
  case RequestType::Read: {
    ASSERT(req.iohandle_.has_value());
    auto& iohandle = req.iohandle_->get();
    iohandle.bytes_to_read_ = result;
    iohandle.cb_(result > 0 ? Event::FileReadyType::Read : Event::FileReadyType::Closed);
    if (result > 0) {
      iohandle.addReadRequest();
    }
    break;
  }
  case RequestType::Connect:
    ASSERT(req.iohandle_.has_value());
    req.iohandle_->get().cb_(result < 0 ? Event::FileReadyType::Closed
                                        : Event::FileReadyType::Write);
    break;
  case RequestType::Write: {
    ASSERT(req.iov_ != nullptr);
    ASSERT(req.iohandle_.has_value());
    auto& iohandle = req.iohandle_->get();
    if (result < 0) {
      delete[] req.iov_;
      iohandle.cb_(Event::FileReadyType::Closed);
    } else {
      iohandle.continueWriting(const_cast<Request&>(req), result);
    }
    break;
  }
  case RequestType::Close:
    break;
  default:
    PANIC("not implemented");
  }
}

void IoUringSocketHandleImpl::FileEventAdapter::onFileEvent() {
  Io::IoUring& uring = io_uring_factory_.get().ref();
  uring.forEveryCompletion([this](void* user_data, int32_t result) {
    auto req = static_cast<Request*>(user_data);
    onRequestCompletion(*req, result);
    delete req;
  });
  uring.submit();
}

void IoUringSocketHandleImpl::FileEventAdapter::initialize(Event::Dispatcher& dispatcher,
                                                           Event::FileReadyCb cb,
                                                           Event::FileTriggerType trigger,
                                                           uint32_t) {
  ASSERT(file_event_ == nullptr, "Attempting to initialize two `file_event_` for the same "
                                 "file descriptor. This is not allowed.");

  cb_ = std::move(cb);
  Io::IoUring& uring = io_uring_factory_.get().ref();
  const os_fd_t event_fd = uring.registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, trigger, Event::FileReadyType::Read);
}

void IoUringSocketHandleImpl::FileEventAdapter::addAcceptRequest() {
  is_accept_added_ = true;
  auto& uring = io_uring_factory_.get().ref();
  auto req = new Request{absl::nullopt, RequestType::Accept};
  auto res = uring.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    uring.submit();
    res = uring.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
  }
}

} // namespace Network
} // namespace Envoy
