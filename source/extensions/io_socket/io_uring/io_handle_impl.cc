#include "source/extensions/io_socket/io_uring/io_handle_impl.h"

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
namespace Extensions {
namespace IoSocket {
namespace IoUring {

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
    IoUringSocketHandleImpl::close();
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ASSERT(SOCKET_VALID(fd_));
  auto req = new Request{absl::nullopt, RequestType::Close};
  io_uring_factory_.getOrCreateUring().prepareClose(fd_, req);
  if (isLeader()) {
    io_uring_factory_.getOrCreateUring().unregisterEventfd();
    file_event_adapter_.reset();
  }
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

bool IoUringSocketHandleImpl::isOpen() const { return SOCKET_VALID(fd_); }
Api::IoCallUint64Result IoUringSocketHandleImpl::readv(uint64_t /* max_length */,
                                                       Buffer::RawSlice* /* slices */,
                                                       uint64_t /* num_slice */) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  const uint64_t max_length = max_length_opt.value_or(UINT64_MAX);
  if (max_length == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  if (bytes_to_read_ == 0) {
    return Api::IoCallUint64Result(
        0, Api::IoErrorPtr(Network::IoSocketError::getIoSocketEagainInstance(),
                           Network::IoSocketError::deleteIoError));
  }

  ASSERT(read_buf_ != nullptr);
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
  return Api::IoCallUint64Result(len,
                                 Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* /*slices */,
                                                        uint64_t /*num_slice*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  auto length = buffer.length();
  ASSERT(length > 0);

  std::list<Buffer::SliceDataPtr> slices;
  while (buffer.length() > 0) {
    // The buffer must not own the data after it has been extracted and put into
    // the `io-uring` submission queue to avoid freeing it before the writev
    // operation is completed.
    Buffer::SliceDataPtr data = buffer.extractMutableFrontSlice();
    slices.push_back(std::move(data));
  }

  uint32_t nr_vecs = slices.size();
  struct iovec* iovecs = new struct iovec[slices.size()];
  struct iovec* iov = iovecs;
  for (auto& slice : slices) {
    absl::Span<uint8_t> mdata = slice->getMutableData();
    iov->iov_base = mdata.data();
    iov->iov_len = mdata.size();
    iov++;
  }

  auto req = new Request{*this, RequestType::Write, iovecs, std::move(slices)};
  io_uring_factory_.getOrCreateUring().prepareWritev(fd_, iovecs, nr_vecs, 0, req);
  return Api::IoCallUint64Result(length,
                                 Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError));
}

Api::IoCallUint64Result
IoUringSocketHandleImpl::sendmsg(const Buffer::RawSlice* /*slices*/, uint64_t /*num_slice*/,
                                 int /*flags*/, const Network::Address::Ip* /*self_ip*/,
                                 const Network::Address::Instance& /*peer_address*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmsg(Buffer::RawSlice* /*slices*/,
                                                         const uint64_t /*num_slice*/,
                                                         uint32_t /*self_port*/,
                                                         RecvMsgOutput& /*output*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmmsg(RawSliceArrays& /*slices*/,
                                                          uint32_t /*self_port*/,
                                                          RecvMsgOutput& /*output*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recv(void* /*buffer*/, size_t /*length*/,
                                                      int /*flags*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

bool IoUringSocketHandleImpl::supportsMmsg() const { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

bool IoUringSocketHandleImpl::supportsUdpGro() const { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

Api::SysCallIntResult
IoUringSocketHandleImpl::bind(Network::Address::InstanceConstSharedPtr address) {
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoUringSocketHandleImpl::listen(int backlog) {
  file_event_adapter_ =
      std::make_unique<FileEventAdapter>(read_buffer_size_, io_uring_factory_, fd_);
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

Network::IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  return file_event_adapter_->accept(addr, addrlen);
}

Api::SysCallIntResult
IoUringSocketHandleImpl::connect(Network::Address::InstanceConstSharedPtr address) {
  auto req = new Request{*this, RequestType::Connect};
  io_uring_factory_.getOrCreateUring().prepareConnect(fd_, address, req);
  if (isLeader()) {
    io_uring_factory_.getOrCreateUring().submit();
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
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

Api::SysCallIntResult IoUringSocketHandleImpl::setBlocking(bool /*blocking*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

absl::optional<int> IoUringSocketHandleImpl::domain() { return domain_; }

Network::Address::InstanceConstSharedPtr IoUringSocketHandleImpl::localAddress() {
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
  return Network::Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

Network::Address::InstanceConstSharedPtr IoUringSocketHandleImpl::peerAddress() {
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
  return Network::Address::addressFromSockAddrOrThrow(ss, ss_len, socket_v6only_);
}

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType trigger, uint32_t events) {
  // Check if this is a server socket accepting new connections.
  if (isLeader()) {
    file_event_adapter_->initialize(dispatcher, cb, trigger, events);
    file_event_adapter_->addAcceptRequest();
    io_uring_factory_.getOrCreateUring().submit();
    return;
  }

  // Check if this is going to become a leading client socket.
  if (!io_uring_factory_.getOrCreateUring().isEventfdRegistered()) {
    file_event_adapter_ =
        std::make_unique<FileEventAdapter>(read_buffer_size_, io_uring_factory_, fd_);
    file_event_adapter_->initialize(dispatcher, cb, trigger, events);
  }

  cb_ = std::move(cb);
}

Network::IoHandlePtr IoUringSocketHandleImpl::duplicate() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

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

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int /*how*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void IoUringSocketHandleImpl::addReadRequest() {
  if (!is_read_enabled_ || !SOCKET_VALID(fd_) || is_read_added_) {
    return;
  }

  ASSERT(read_buf_ == nullptr);
  is_read_added_ = true; // don't add READ if it's been already added.
  read_buf_ = std::unique_ptr<uint8_t[]>(new uint8_t[read_buffer_size_]);
  iov_.iov_base = read_buf_.get();
  iov_.iov_len = read_buffer_size_;
  auto req = new Request{*this, RequestType::Read};
  io_uring_factory_.getOrCreateUring().prepareReadv(fd_, &iov_, 1, 0, req);
}

Network::IoHandlePtr IoUringSocketHandleImpl::FileEventAdapter::accept(struct sockaddr* addr,
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
    ENVOY_LOG(debug, "async request of type {} failed: {}", req.type_, errorDetails(-result));
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
  case RequestType::Write:
    ASSERT(req.iov_ != nullptr);
    ASSERT(req.iohandle_.has_value());
    delete[] req.iov_;
    if (result < 0) {
      req.iohandle_->get().cb_(Event::FileReadyType::Closed);
    }
    break;
  case RequestType::Close:
    break;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

void IoUringSocketHandleImpl::FileEventAdapter::onFileEvent() {
  Io::IoUring& uring = io_uring_factory_.getOrCreateUring();
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
                                                           uint32_t events) {
  ASSERT(file_event_ == nullptr, "Attempting to initialize two `file_event_` for the same "
                                 "file descriptor. This is not allowed.");

  cb_ = std::move(cb);
  Io::IoUring& uring = io_uring_factory_.getOrCreateUring();
  const os_fd_t event_fd = uring.registerEventfd();
  file_event_ = dispatcher.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, trigger, events);
}

void IoUringSocketHandleImpl::FileEventAdapter::addAcceptRequest() {
  is_accept_added_ = true;
  auto req = new Request{absl::nullopt, RequestType::Accept};
  io_uring_factory_.getOrCreateUring().prepareAccept(fd_, &remote_addr_, &remote_addr_len_, req);
}

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
