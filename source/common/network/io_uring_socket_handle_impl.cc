#include "source/common/network/io_uring_socket_handle_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface_impl.h"

namespace Envoy {
namespace Network {

IoUringSocketHandleImpl::IoUringSocketHandleImpl(Io::IoUringWorkerFactory& io_uring_worker_factory,
                                                 os_fd_t fd, bool socket_v6only,
                                                 absl::optional<int> domain, bool is_server_socket)
    : IoSocketHandleBaseImpl(fd, socket_v6only, domain),
      io_uring_worker_factory_(io_uring_worker_factory),
      io_uring_socket_type_(is_server_socket ? IoUringSocketType::Server
                                             : IoUringSocketType::Unknown) {
  ENVOY_LOG(trace, "construct io uring socket handle, fd = {}, type = {}", fd_,
            ioUringSocketTypeStr());
}

IoUringSocketHandleImpl::~IoUringSocketHandleImpl() {
  ENVOY_LOG(trace, "~IoUringSocketHandleImpl, type = {}", ioUringSocketTypeStr());

  if (SOCKET_INVALID(fd_)) {
    return;
  }

  // If the socket is owned by the main thread like a listener, it may outlive the IoUringWorker.
  // We have to ensure that the current thread has been registered and the io_uring in the thread
  // is still available.
  // TODO(zhxie): for current usage of server socket and client socket, the check may be
  // redundant.
  if (io_uring_socket_type_ != IoUringSocketType::Unknown &&
      io_uring_socket_type_ != IoUringSocketType::Accept &&
      io_uring_worker_factory_.currentThreadRegistered() && io_uring_socket_.has_value()) {
    if (io_uring_socket_->getStatus() != Io::IoUringSocketStatus::Closed) {
      io_uring_socket_.ref().close(false);
    }
  } else {
    // The TLS slot has been shut down by this moment with io_uring wiped out, thus use the
    // POSIX system call instead of IoUringSocketHandleImpl::close().
    ::close(fd_);
  }
}

Api::IoCallUint64Result IoUringSocketHandleImpl::close() {
  ENVOY_LOG(trace, "close, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  ASSERT(SOCKET_VALID(fd_));

  if (io_uring_socket_type_ == IoUringSocketType::Unknown ||
      io_uring_socket_type_ == IoUringSocketType::Accept || !io_uring_socket_.has_value()) {
    if (file_event_) {
      file_event_.reset();
    }
    ::close(fd_);
  } else {
    io_uring_socket_.ref().close(false);
    io_uring_socket_.reset();
  }
  SET_SOCKET_INVALID(fd_);
  return Api::ioCallUint64ResultNoError();
}

Api::IoCallUint64Result
IoUringSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices, uint64_t num_slice) {
  ENVOY_LOG(debug, "readv, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  Api::IoCallUint64Result result = copyOut(max_length, slices, num_slice);
  if (result.ok()) {
    // If the return value is 0, there should be a remote close. Return the value directly.
    if (result.return_value_ != 0) {
      io_uring_socket_->getReadParam()->buf_.drain(result.return_value_);
    }
  }
  return result;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::read(Buffer::Instance& buffer,
                                                      absl::optional<uint64_t> max_length_opt) {
  ENVOY_LOG(trace, "read, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  absl::optional<Api::IoCallUint64Result> read_result = checkReadResult();
  if (read_result.has_value()) {
    return std::move(*read_result);
  }

  const OptRef<Io::ReadParam>& read_param = io_uring_socket_->getReadParam();
  uint64_t max_read_length =
      std::min(max_length_opt.value_or(UINT64_MAX), read_param->buf_.length());
  buffer.move(read_param->buf_, max_read_length);
  return {max_read_length, IoSocketError::none()};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                        uint64_t num_slice) {
  ENVOY_LOG(trace, "writev, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  absl::optional<Api::IoCallUint64Result> write_result = checkWriteResult();
  if (write_result.has_value()) {
    return std::move(*write_result);
  }

  uint64_t ret = io_uring_socket_->write(slices, num_slice);
  return {ret, IoSocketError::none()};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::write(Buffer::Instance& buffer) {
  ENVOY_LOG(trace, "write {}, fd = {}, type = {}", buffer.length(), fd_, ioUringSocketTypeStr());

  absl::optional<Api::IoCallUint64Result> write_result = checkWriteResult();
  if (write_result.has_value()) {
    return std::move(*write_result);
  }

  uint64_t buffer_size = buffer.length();
  io_uring_socket_->write(buffer);
  return {buffer_size, IoSocketError::none()};
}

Api::IoCallUint64Result IoUringSocketHandleImpl::sendmsg(const Buffer::RawSlice*, uint64_t, int,
                                                         const Address::Ip*,
                                                         const Address::Instance&) {
  ENVOY_LOG(trace, "sendmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmsg(Buffer::RawSlice*, const uint64_t,
                                                         uint32_t,
                                                         const IoHandle::UdpSaveCmsgConfig&,
                                                         RecvMsgOutput&) {
  ENVOY_LOG(trace, "recvmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recvmmsg(RawSliceArrays&, uint32_t,
                                                          const IoHandle::UdpSaveCmsgConfig&,
                                                          RecvMsgOutput&) {
  ENVOY_LOG(trace, "recvmmsg, fd = {}, type = {}", fd_, ioUringSocketTypeStr());
  return Network::IoSocketError::ioResultSocketInvalidAddress();
}

Api::IoCallUint64Result IoUringSocketHandleImpl::recv(void* buffer, size_t length, int flags) {
  ASSERT(io_uring_socket_.has_value());
  ENVOY_LOG(trace, "recv, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  // The only used flag in Envoy is MSG_PEEK for listener filters, including TLS inspectors.
  ASSERT(flags == 0 || flags == MSG_PEEK);
  Buffer::RawSlice slice;
  slice.mem_ = buffer;
  slice.len_ = length;
  if (flags == 0) {
    return readv(length, &slice, 1);
  }

  return copyOut(length, &slice, 1);
}

Api::SysCallIntResult IoUringSocketHandleImpl::bind(Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "bind {}, fd = {}, io_uring_socket_type = {}", address->asString(), fd_,
            ioUringSocketTypeStr());
  return Api::OsSysCallsSingleton::get().bind(fd_, address->sockAddr(), address->sockAddrLen());
}

Api::SysCallIntResult IoUringSocketHandleImpl::listen(int backlog) {
  ENVOY_LOG(trace, "listen, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  ASSERT(io_uring_socket_type_ == IoUringSocketType::Unknown);

  io_uring_socket_type_ = IoUringSocketType::Accept;
  setBlocking(false);
  return Api::OsSysCallsSingleton::get().listen(fd_, backlog);
}

IoHandlePtr IoUringSocketHandleImpl::accept(struct sockaddr* addr, socklen_t* addrlen) {
  ENVOY_LOG(trace, "accept, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  ASSERT(io_uring_socket_type_ == IoUringSocketType::Accept);

  Envoy::Api::SysCallSocketResult result =
      Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
  if (SOCKET_INVALID(result.return_value_)) {
    return nullptr;
  }
  return std::make_unique<IoUringSocketHandleImpl>(io_uring_worker_factory_, result.return_value_,
                                                   socket_v6only_, domain_, true);
}

Api::SysCallIntResult IoUringSocketHandleImpl::connect(Address::InstanceConstSharedPtr address) {
  ENVOY_LOG(trace, "connect, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  ASSERT(io_uring_socket_type_ == IoUringSocketType::Client);

  io_uring_socket_->connect(address);
  return Api::SysCallIntResult{-1, EINPROGRESS};
}

Api::SysCallIntResult IoUringSocketHandleImpl::getOption(int level, int optname, void* optval,
                                                         socklen_t* optlen) {
  // io_uring socket does not populate connect error in getsockopt. Instead, the connect error is
  // returned in onConnect() handling. We will imitate the default socket behavior here for client
  // socket with optname SO_ERROR, which is only used to check connect error.
  if (io_uring_socket_type_ == IoUringSocketType::Client && optname == SO_ERROR &&
      io_uring_socket_.has_value()) {
    int* intval = static_cast<int*>(optval);
    *intval = -io_uring_socket_->getWriteParam()->result_;
    *optlen = sizeof(int);
    return {0, 0};
  }

  return IoSocketHandleBaseImpl::getOption(level, optname, optval, optlen);
}

IoHandlePtr IoUringSocketHandleImpl::duplicate() {
  ENVOY_LOG(trace, "duplicate, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().duplicate(fd_);
  RELEASE_ASSERT(result.return_value_ != -1,
                 fmt::format("duplicate failed for '{}': ({}) {}", fd_, result.errno_,
                             errorDetails(result.errno_)));
  return SocketInterfaceImpl::makePlatformSpecificSocket(result.return_value_, socket_v6only_,
                                                         domain_, Network::SocketCreationOptions{},
                                                         &io_uring_worker_factory_);
}

void IoUringSocketHandleImpl::initializeFileEvent(Event::Dispatcher& dispatcher,
                                                  Event::FileReadyCb cb,
                                                  Event::FileTriggerType trigger, uint32_t events) {
  ENVOY_LOG(trace, "initialize file event, fd = {}, type = {}, has socket = {}", fd_,
            ioUringSocketTypeStr(), io_uring_socket_.has_value());

  // The IoUringSocket has already been created. It usually happened after a resetFileEvents.
  if (io_uring_socket_.has_value()) {
    if (&io_uring_socket_->getIoUringWorker().dispatcher() ==
        &io_uring_worker_factory_.getIoUringWorker()->dispatcher()) {
      io_uring_socket_->setFileReadyCb(std::move(cb));
      io_uring_socket_->enableRead();
      io_uring_socket_->enableCloseEvent(events & Event::FileReadyType::Closed);
    } else {
      ENVOY_LOG(trace, "initialize file event from another thread, fd = {}, type = {}", fd_,
                ioUringSocketTypeStr());
      Thread::CondVar wait_cv;
      Thread::MutexBasicLockable mutex;
      Buffer::OwnedImpl buf;
      os_fd_t fd = io_uring_socket_->fd();

      {
        Thread::LockGuard lock(mutex);
        // Close the original socket in its running thread.
        io_uring_socket_->getIoUringWorker().dispatcher().post(
            [&origin_socket = io_uring_socket_, &wait_cv, &mutex, &buf]() {
              // Move the data of original socket's read buffer to the temporary buf.
              origin_socket->close(true, [&wait_cv, &mutex, &buf](Buffer::Instance& buffer) {
                Thread::LockGuard lock(mutex);
                buf.move(buffer);
                wait_cv.notifyOne();
              });
            });
        wait_cv.wait(mutex);
      }

      // Move the temporary buf to the newly created one.
      io_uring_socket_ = io_uring_worker_factory_.getIoUringWorker()->addServerSocket(
          fd, buf, std::move(cb), events & Event::FileReadyType::Closed);
    }
    return;
  }

  switch (io_uring_socket_type_) {
  case IoUringSocketType::Accept:
    file_event_ = dispatcher.createFileEvent(fd_, cb, trigger, events);
    break;
  case IoUringSocketType::Server:
    io_uring_socket_ = io_uring_worker_factory_.getIoUringWorker()->addServerSocket(
        fd_, std::move(cb), events & Event::FileReadyType::Closed);
    break;
  case IoUringSocketType::Unknown:
  case IoUringSocketType::Client:
    io_uring_socket_type_ = IoUringSocketType::Client;
    io_uring_socket_ = io_uring_worker_factory_.getIoUringWorker()->addClientSocket(
        fd_, std::move(cb), events & Event::FileReadyType::Closed);
    break;
  }
}

void IoUringSocketHandleImpl::activateFileEvents(uint32_t events) {
  ENVOY_LOG(trace, "activate file events {}, fd = {}, type = {}", events, fd_,
            ioUringSocketTypeStr());

  if (io_uring_socket_type_ == IoUringSocketType::Accept) {
    ASSERT(file_event_ != nullptr);
    file_event_->activate(events);
    return;
  }

  if (events & Event::FileReadyType::Read) {
    io_uring_socket_->injectCompletion(Io::Request::RequestType::Read);
  }
  if (events & Event::FileReadyType::Write) {
    io_uring_socket_->injectCompletion(Io::Request::RequestType::Write);
  }
}

void IoUringSocketHandleImpl::enableFileEvents(uint32_t events) {
  ENVOY_LOG(trace, "enable file events {}, fd = {}, type = {}", events, fd_,
            ioUringSocketTypeStr());

  if (io_uring_socket_type_ == IoUringSocketType::Accept) {
    ASSERT(file_event_ != nullptr);
    file_event_->setEnabled(events);
    return;
  }

  if (events & Event::FileReadyType::Read) {
    io_uring_socket_->enableRead();
  } else {
    io_uring_socket_->disableRead();
  }
  io_uring_socket_->enableCloseEvent(events & Event::FileReadyType::Closed);
}

void IoUringSocketHandleImpl::resetFileEvents() {
  ENVOY_LOG(trace, "reset file events, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  if (io_uring_socket_type_ == IoUringSocketType::Accept) {
    file_event_.reset();
    return;
  }

  io_uring_socket_->disableRead();
  io_uring_socket_->enableCloseEvent(false);
}

Api::SysCallIntResult IoUringSocketHandleImpl::shutdown(int how) {
  ENVOY_LOG(trace, "shutdown, fd = {}, type = {}", fd_, ioUringSocketTypeStr());

  ASSERT(io_uring_socket_type_ == IoUringSocketType::Server ||
         io_uring_socket_type_ == IoUringSocketType::Client);

  io_uring_socket_->shutdown(how);
  return Api::SysCallIntResult{0, 0};
}

absl::optional<Api::IoCallUint64Result> IoUringSocketHandleImpl::checkReadResult() const {
  ASSERT(io_uring_socket_.has_value());
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Server ||
         io_uring_socket_type_ == IoUringSocketType::Client);

  const OptRef<Io::ReadParam>& read_param = io_uring_socket_->getReadParam();
  // A absl::nullopt read param means that there is no io_uring request which has been done.
  if (read_param == absl::nullopt) {
    if (io_uring_socket_->getStatus() != Io::IoUringSocketStatus::RemoteClosed) {
      return Api::IoCallUint64Result{0, IoSocketError::getIoSocketEagainError()};
    } else {
      ENVOY_LOG(trace, "read, fd = {}, type = {}, remote close", fd_, ioUringSocketTypeStr());
      return Api::ioCallUint64ResultNoError();
    }
  }

  if (read_param->result_ == 0) {
    ENVOY_LOG(trace, "read remote close, fd = {}, type = {}", fd_, ioUringSocketTypeStr());
    return Api::ioCallUint64ResultNoError();
  }

  if (read_param->result_ < 0) {
    ASSERT(read_param->buf_.length() == 0);
    ENVOY_LOG(trace, "read error = {}, fd = {}, type = {}", -read_param->result_, fd_,
              ioUringSocketTypeStr());
    if (read_param->result_ == -EAGAIN) {
      return Api::IoCallUint64Result{0, IoSocketError::getIoSocketEagainError()};
    }
    return Api::IoCallUint64Result{0, IoSocketError::create(-read_param->result_)};
  }

  // The buffer has been read in the previous call, return EAGAIN to tell the caller to wait for
  // the next read event.
  if (read_param->buf_.length() == 0) {
    return Api::IoCallUint64Result{0, IoSocketError::getIoSocketEagainError()};
  }
  return absl::nullopt;
}

absl::optional<Api::IoCallUint64Result> IoUringSocketHandleImpl::checkWriteResult() const {
  ASSERT(io_uring_socket_.has_value());
  ASSERT(io_uring_socket_type_ == IoUringSocketType::Server ||
         io_uring_socket_type_ == IoUringSocketType::Client);

  const OptRef<Io::WriteParam>& write_param = io_uring_socket_->getWriteParam();
  if (write_param != absl::nullopt) {
    // EAGAIN indicates an injected write event to trigger IO handle write. Submit the new write to
    // the io_uring.
    if (write_param->result_ < 0 && write_param->result_ != -EAGAIN) {
      return Api::IoCallUint64Result{0, IoSocketError::create(-write_param->result_)};
    }
  }
  return absl::nullopt;
}

Api::IoCallUint64Result IoUringSocketHandleImpl::copyOut(uint64_t max_length,
                                                         Buffer::RawSlice* slices,
                                                         uint64_t num_slice) {
  absl::optional<Api::IoCallUint64Result> read_result = checkReadResult();
  if (read_result.has_value()) {
    return std::move(*read_result);
  }

  const OptRef<Io::ReadParam>& read_param = io_uring_socket_->getReadParam();
  const uint64_t max_read_length = std::min(max_length, static_cast<uint64_t>(read_param->result_));
  uint64_t num_bytes_to_read = read_param->buf_.copyOutToSlices(max_read_length, slices, num_slice);
  return {num_bytes_to_read, IoSocketError::none()};
}

} // namespace Network
} // namespace Envoy
