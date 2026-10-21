#include "source/extensions/transport_sockets/dynamic_modules/splice_pump.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/tls.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

namespace {
// TLS record type bytes used by the control record dispatch (RFC 5246 6.2.1, RFC 8446).
constexpr uint8_t TlsRecordChangeCipherSpec = 20;
constexpr uint8_t TlsRecordAlert = 21;
constexpr uint8_t TlsRecordHandshake = 22;
constexpr uint8_t TlsHandshakeNewSessionTicket = 4;
constexpr uint8_t TlsAlertCloseNotify = 0;
} // namespace

ControlAction classifyKtlsControlRecord(uint8_t record_type, const uint8_t* data, size_t len) {
  switch (record_type) {
  case TlsRecordAlert:
    if (len >= 2 && data[1] == TlsAlertCloseNotify) {
      return ControlAction::Eof;
    }
    return ControlAction::Close;
  case TlsRecordHandshake: {
    // Walk the coalesced handshake messages, each a 1 byte type then a 3 byte length. Tolerate a
    // NewSessionTicket and close on anything else the splice path cannot service.
    size_t pos = 0;
    while (pos < len) {
      if (data[pos] != TlsHandshakeNewSessionTicket || pos + 4 > len) {
        return ControlAction::Close;
      }
      const size_t msg_len = (static_cast<size_t>(data[pos + 1]) << 16) |
                             (static_cast<size_t>(data[pos + 2]) << 8) |
                             static_cast<size_t>(data[pos + 3]);
      if (msg_len > len - pos - 4) {
        return ControlAction::Close;
      }
      pos += 4 + msg_len;
    }
    return ControlAction::Retry;
  }
  case TlsRecordChangeCipherSpec:
    return ControlAction::Retry; // TLS 1.3 compatibility record.
  default:
    return ControlAction::Close;
  }
}

#if defined(__linux__)

namespace {
constexpr uint8_t TlsRecordApplicationData = 23;
// Requested pipe capacity. An unprivileged process cannot exceed fs.pipe-max-size, so a larger
// request fails and the pipe keeps its default size.
constexpr size_t PipeCapacity = 1024 * 1024;
// Upper bound on non-data kTLS records drained in one pass. A trusted upstream sends a handful of
// NewSessionTickets, so a long run signals a misbehaving peer and the pump closes.
constexpr int MaxControlRecordsPerPass = 1024;
// Bytes read from the kTLS socket per recvmsg. TLS caps a record at 16384 bytes of plaintext, so a
// larger read lets the kernel return several application data records in one call. Each kTLS
// session holds this buffer for its lifetime.
constexpr size_t DownloadReadSize = 64 * 1024;
constexpr unsigned SpliceFlags = SPLICE_F_NONBLOCK | SPLICE_F_MOVE;

// Counts the directions of fd with kTLS installed. Reading only the version and cipher header
// leaves the keys in the kernel.
int kernelTlsDirections(os_fd_t fd) {
  int directions = 0;
  for (const int direction : {TLS_TX, TLS_RX}) {
    struct tls_crypto_info info;
    socklen_t len = sizeof(info);
    if (::getsockopt(fd, SOL_TLS, direction, &info, &len) == 0) {
      ++directions;
    }
  }
  return directions;
}
} // namespace

SplicePump::SplicePump(os_fd_t down_fd, os_fd_t up_fd, Event::Dispatcher& dispatcher,
                       CompletionCb on_complete, uint64_t max_bytes_per_wakeup)
    : down_fd_(down_fd), up_fd_(up_fd), dispatcher_(dispatcher),
      on_complete_(std::move(on_complete)), max_bytes_per_wakeup_(max_bytes_per_wakeup) {
  // A zero budget would yield before any work and resume forever.
  ASSERT(max_bytes_per_wakeup_ > 0);
}

SplicePump::~SplicePump() {
  // Close only the pipe fds. The socket fds are borrowed and closed by their connections.
  for (int fd : {u2d_.read_fd, u2d_.write_fd, d2u_.read_fd, d2u_.write_fd}) {
    if (fd >= 0) {
      ::close(fd);
    }
  }
}

bool SplicePump::prepare(bool upstream_needs_ktls) {
  // A blocking socket would stall the worker inside splice. io_uring creates its upstream sockets
  // blocking, so this also keeps an io_uring pair on the buffered path.
  for (const os_fd_t fd : {down_fd_, up_fd_}) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || (flags & O_NONBLOCK) == 0) {
      ENVOY_LOG(debug, "splice pump needs non-blocking sockets");
      return false;
    }
  }
  // Splice from a kTLS RX socket can truncate, so the downstream must be plaintext. A kTLS upstream
  // needs both directions, the receive side for recvmsg and the send side to encrypt the upload.
  const int up_tls_directions = kernelTlsDirections(up_fd_);
  if (kernelTlsDirections(down_fd_) != 0 || up_tls_directions == 1) {
    ENVOY_LOG(debug,
              "splice pump needs a plaintext downstream and a plaintext or full kTLS upstream");
    return false;
  }
  // A secure upstream without kTLS encrypts in the module, which the splice would bypass.
  if (upstream_needs_ktls && up_tls_directions != 2) {
    ENVOY_LOG(debug, "splice pump needs kTLS on a secure upstream");
    return false;
  }
  up_is_ktls_ = up_tls_directions == 2;
  // A kTLS download is read into download_buf_ and written straight to the downstream socket, so it
  // needs no u2d pipe.
  if (up_is_ktls_) {
    download_buf_.resize(DownloadReadSize);
  }
  for (Pipe* pipe : {&u2d_, &d2u_}) {
    if (pipe == &u2d_ && up_is_ktls_) {
      continue;
    }
    int fds[2];
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
      ENVOY_LOG_EVERY_POW_2(warn, "splice pump pipe2 failed, {}", errorDetails(errno));
      return false;
    }
    pipe->read_fd = fds[0];
    pipe->write_fd = fds[1];
  }
  return true;
}

void SplicePump::arm() {
  // Record the capacity the kernel actually gave each pipe. The resize returns the page rounded
  // size on success, otherwise the pipe keeps its default size. Trusting the requested size while
  // the real pipe is smaller would splice into a full pipe and read the EAGAIN as a drained socket.
  for (Pipe* pipe : {&u2d_, &d2u_}) {
    if (pipe->write_fd < 0) {
      continue;
    }
    int size = ::fcntl(pipe->write_fd, F_SETPIPE_SZ, static_cast<int>(PipeCapacity));
    if (size <= 0) {
      size = ::fcntl(pipe->write_fd, F_GETPIPE_SZ);
    }
    RELEASE_ASSERT(size > 0, "splice pump could not read the size of a new pipe");
    pipe->capacity = static_cast<size_t>(size);
  }

  up_file_event_ = dispatcher_.createFileEvent(
      up_fd_, [this](uint32_t events) { return onUpReady(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed);
  down_file_event_ = dispatcher_.createFileEvent(
      down_fd_, [this](uint32_t events) { return onDownReady(events); },
      Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed);
  resume_cb_ = dispatcher_.createSchedulableCallback([this]() { pump(); });

  // The sockets may already hold data at engage time, so assume both are readable and let the first
  // reads clear the latches on EAGAIN.
  up_readable_ = down_readable_ = true;
  ENVOY_LOG(debug, "splice pump armed down_fd={} up_fd={} kTLS={}", down_fd_, up_fd_, up_is_ktls_);
  pump();
}

absl::Status SplicePump::onUpReady(uint32_t events) {
  if (events & (Event::FileReadyType::Read | Event::FileReadyType::Closed)) {
    up_readable_ = true;
  }
  pump();
  return absl::OkStatus();
}

absl::Status SplicePump::onDownReady(uint32_t events) {
  if (events & (Event::FileReadyType::Read | Event::FileReadyType::Closed)) {
    down_readable_ = true;
  }
  pump();
  return absl::OkStatus();
}

void SplicePump::pump() {
  if (completed_) {
    return;
  }
  // Assume both sinks are writable at the start of every pass and let a real EAGAIN clear that for
  // the pass. The write edge of a full sink wakes the pump to retry, so the pump never re-arms its
  // own events, which would spin the loop while a sink stays full.
  bool up_writable = true;
  bool down_writable = true;
  int control_records = 0;
  const uint64_t delivered_before = bytes_uploaded_ + bytes_downloaded_;
  const auto budget_spent = [&] {
    return bytes_uploaded_ + bytes_downloaded_ - delivered_before >= max_bytes_per_wakeup_;
  };
  bool progress = true;
  while (progress) {
    // A fast pair could keep the loop busy without end, so yield once the budget is spent and
    // finish on the next iteration.
    if (budget_spent()) {
      resume_cb_->scheduleCallbackNextIteration();
      break;
    }
    progress = false;

    // (1) Drain the u2d pipe to the downstream socket.
    while (down_writable && u2d_.in_pipe > 0) {
      const ssize_t n =
          ::splice(u2d_.read_fd, nullptr, down_fd_, nullptr, u2d_.in_pipe, SpliceFlags);
      if (n > 0) {
        u2d_.in_pipe -= static_cast<size_t>(n);
        bytes_downloaded_ += static_cast<uint64_t>(n);
        progress = true;
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        down_writable = false;
      } else {
        ENVOY_LOG(debug, "splice pump downstream write error, {}", errorDetails(errno));
        complete(Outcome::DownstreamError);
        return;
      }
    }

    // (2) Read the download. A kTLS upstream is read with recvmsg into download_buf_ and written
    // straight to the downstream socket, and a short write keeps the rest for the next pass. A
    // plaintext upstream splices into the u2d pipe.
    if (up_is_ktls_) {
      while (true) {
        while (down_writable && download_offset_ < download_len_) {
          const ssize_t n = ::send(down_fd_, download_buf_.data() + download_offset_,
                                   download_len_ - download_offset_, MSG_DONTWAIT | MSG_NOSIGNAL);
          if (n > 0) {
            download_offset_ += static_cast<size_t>(n);
            bytes_downloaded_ += static_cast<uint64_t>(n);
            progress = true;
          } else if (n < 0 && errno == EINTR) {
            continue;
          } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            down_writable = false;
          } else {
            ENVOY_LOG(debug, "splice pump downstream write error, {}", errorDetails(errno));
            complete(Outcome::DownstreamError);
            return;
          }
        }
        // Stop reading once the budget is spent too, because the direct socket writes have no pipe
        // to bound this loop. The outer check then schedules the resume.
        if (download_offset_ < download_len_ || !up_readable_ || up_read_eof_ || budget_spent()) {
          break;
        }
        const ReadResult result = readUpstreamRecord();
        if (result == ReadResult::Stop) {
          if (completed_) {
            return;
          }
          break;
        }
        progress = true;
        if (result == ReadResult::Control && ++control_records > MaxControlRecordsPerPass) {
          ENVOY_LOG(debug, "splice pump too many kTLS control records, closing");
          complete(Outcome::UpstreamError);
          return;
        }
      }
    } else {
      while (up_readable_ && !up_read_eof_ && u2d_.in_pipe < u2d_.capacity) {
        const ssize_t n = ::splice(up_fd_, nullptr, u2d_.write_fd, nullptr,
                                   u2d_.capacity - u2d_.in_pipe, SpliceFlags);
        if (n > 0) {
          u2d_.in_pipe += static_cast<size_t>(n);
          progress = true;
        } else if (n == 0) {
          up_read_eof_ = true;
        } else if (errno == EINTR) {
          continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // A full pipe also yields EAGAIN, so only an empty pipe proves the socket is drained.
          if (u2d_.in_pipe == 0) {
            up_readable_ = false;
          }
          break;
        } else {
          ENVOY_LOG(debug, "splice pump download read error, {}", errorDetails(errno));
          complete(Outcome::UpstreamError);
          return;
        }
      }
    }

    // (3) Drain the d2u pipe to the upstream socket. A kTLS TX socket encrypts and frames the bytes
    // into TLS records in-kernel.
    while (up_writable && d2u_.in_pipe > 0) {
      const ssize_t n = ::splice(d2u_.read_fd, nullptr, up_fd_, nullptr, d2u_.in_pipe, SpliceFlags);
      if (n > 0) {
        d2u_.in_pipe -= static_cast<size_t>(n);
        bytes_uploaded_ += static_cast<uint64_t>(n);
        progress = true;
      } else if (n < 0 && errno == EINTR) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        up_writable = false;
      } else {
        ENVOY_LOG(debug, "splice pump upstream write error, {}", errorDetails(errno));
        complete(Outcome::UpstreamError);
        return;
      }
    }

    // (4) Read the plaintext upload from the downstream socket into the d2u pipe.
    while (down_readable_ && !down_read_eof_ && d2u_.in_pipe < d2u_.capacity) {
      const ssize_t n = ::splice(down_fd_, nullptr, d2u_.write_fd, nullptr,
                                 d2u_.capacity - d2u_.in_pipe, SpliceFlags);
      if (n > 0) {
        d2u_.in_pipe += static_cast<size_t>(n);
        progress = true;
      } else if (n == 0) {
        down_read_eof_ = true;
      } else if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // A full pipe also yields EAGAIN, so only an empty pipe proves the socket is drained.
        if (d2u_.in_pipe == 0) {
          down_readable_ = false;
        }
        break;
      } else {
        ENVOY_LOG(debug, "splice pump upload read error, {}", errorDetails(errno));
        complete(Outcome::DownstreamError);
        return;
      }
    }
  }

  maybeComplete();
}

SplicePump::ReadResult SplicePump::readUpstreamRecord() {
  alignas(struct cmsghdr) char cmsg_space[CMSG_SPACE(sizeof(uint8_t))];
  struct iovec iov;
  iov.iov_base = download_buf_.data();
  iov.iov_len = download_buf_.size();
  struct msghdr msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg_space;
  msg.msg_controllen = sizeof(cmsg_space);

  ssize_t n;
  do {
    n = ::recvmsg(up_fd_, &msg, MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      up_readable_ = false;
      return ReadResult::Stop;
    }
    ENVOY_LOG(debug, "splice pump kTLS recvmsg error, {}", errorDetails(errno));
    complete(Outcome::UpstreamError);
    return ReadResult::Stop;
  }
  if (n == 0) {
    up_read_eof_ = true;
    return ReadResult::Stop;
  }
  if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
    ENVOY_LOG(debug, "splice pump kTLS record truncated");
    complete(Outcome::UpstreamError);
    return ReadResult::Stop;
  }

  uint8_t record_type = 0;
  for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_TLS && cmsg->cmsg_type == TLS_GET_RECORD_TYPE &&
        cmsg->cmsg_len >= CMSG_LEN(sizeof(uint8_t))) {
      record_type = *reinterpret_cast<uint8_t*>(CMSG_DATA(cmsg));
    }
  }

  if (record_type == TlsRecordApplicationData) {
    download_len_ = static_cast<size_t>(n);
    download_offset_ = 0;
    return ReadResult::Data;
  }

  switch (classifyKtlsControlRecord(record_type, download_buf_.data(), static_cast<size_t>(n))) {
  case ControlAction::Retry:
    return ReadResult::Control;
  case ControlAction::Eof:
    up_read_eof_ = true;
    return ReadResult::Stop;
  case ControlAction::Close:
    ENVOY_LOG(debug, "splice pump closing on TLS record type {}", record_type);
    complete(Outcome::UpstreamError);
    return ReadResult::Stop;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool SplicePump::sendUpstreamCloseNotify() {
  // Emit a TLS close_notify on the kTLS TX socket via TLS_SET_RECORD_TYPE before the FIN, so a
  // strict peer does not treat the upload as truncated.
  uint8_t alert[2] = {1, TlsAlertCloseNotify};
  alignas(struct cmsghdr) char cmsg_space[CMSG_SPACE(sizeof(uint8_t))];
  struct iovec iov;
  iov.iov_base = alert;
  iov.iov_len = sizeof(alert);
  struct msghdr msg;
  std::memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg_space;
  msg.msg_controllen = sizeof(cmsg_space);
  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_TLS;
  cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
  cmsg->cmsg_len = CMSG_LEN(sizeof(uint8_t));
  *CMSG_DATA(cmsg) = TlsRecordAlert;
  ssize_t rc;
  do {
    rc = ::sendmsg(up_fd_, &msg, MSG_DONTWAIT);
  } while (rc < 0 && errno == EINTR);
  if (rc >= 0) {
    return true;
  }
  // A full TX buffer is worth a retry. Any other error means the upstream failed, for example
  // after a reset that no read is left to see.
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    ENVOY_LOG(debug, "splice pump close_notify error, {}", errorDetails(errno));
    complete(Outcome::UpstreamError);
  }
  return false;
}

void SplicePump::maybeComplete() {
  // A plaintext download drains through the u2d pipe and a kTLS one through download_buf_, so each
  // path leaves the other one empty.
  const bool u2d_drained = up_read_eof_ && u2d_.in_pipe == 0 && download_offset_ == download_len_;
  const bool d2u_drained = down_read_eof_ && d2u_.in_pipe == 0;
  // Forward each end of stream as a half-close once its source is drained, so the other direction
  // keeps flowing until it ends too.
  if (u2d_drained && !down_write_shutdown_) {
    ::shutdown(down_fd_, SHUT_WR);
    down_write_shutdown_ = true;
  }
  if (d2u_drained && !up_write_shutdown_) {
    // The write edge of a full kTLS TX buffer wakes the pump to retry the alert before the FIN.
    if (up_is_ktls_ && !sendUpstreamCloseNotify()) {
      return;
    }
    ::shutdown(up_fd_, SHUT_WR);
    up_write_shutdown_ = true;
  }
  if (u2d_drained && d2u_drained) {
    complete(Outcome::Done);
  }
}

void SplicePump::complete(Outcome outcome) {
  if (completed_) {
    return;
  }
  completed_ = true;
  ENVOY_LOG(debug, "splice pump complete, outcome={}", static_cast<int>(outcome));
  // Disable rather than destroy the file events, because this may run inside one of their
  // callbacks. The caller destroys the pump later through a deferred teardown.
  up_file_event_->setEnabled(0);
  down_file_event_->setEnabled(0);
  on_complete_(outcome);
}

#else // !defined(__linux__)

// splice and kTLS are Linux only. Elsewhere prepare fails, so the caller stays on the buffered path
// and every other method is an unused no-op.
SplicePump::SplicePump(os_fd_t down_fd, os_fd_t up_fd, Event::Dispatcher& dispatcher,
                       CompletionCb on_complete, uint64_t max_bytes_per_wakeup)
    : down_fd_(down_fd), up_fd_(up_fd), dispatcher_(dispatcher),
      on_complete_(std::move(on_complete)), max_bytes_per_wakeup_(max_bytes_per_wakeup) {}

SplicePump::~SplicePump() = default;
bool SplicePump::prepare(bool) { return false; }
void SplicePump::arm() {}
absl::Status SplicePump::onUpReady(uint32_t) { return absl::OkStatus(); }
absl::Status SplicePump::onDownReady(uint32_t) { return absl::OkStatus(); }
void SplicePump::pump() {}
SplicePump::ReadResult SplicePump::readUpstreamRecord() { return ReadResult::Stop; }
bool SplicePump::sendUpstreamCloseNotify() { return true; }
void SplicePump::maybeComplete() {}
void SplicePump::complete(Outcome) {}

#endif

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
