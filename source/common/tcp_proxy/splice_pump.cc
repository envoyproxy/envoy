#include "source/common/tcp_proxy/splice_pump.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>

// spliceLegIsRawOrKtls dereferences KtlsBytestreamInfo, which envoy/network/connection.h only
// forward-declares. The full struct lives here.
#include "envoy/network/transport_socket.h"

#if defined(__linux__)
#include <fcntl.h>
#include <linux/tls.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace Envoy {
namespace TcpProxy {

namespace {
// TLS record-type bytes for kTLS control-message dispatch.
constexpr uint8_t kTlsRecordChangeCipherSpec = 20;
constexpr uint8_t kTlsRecordAlert = 21;
constexpr uint8_t kTlsRecordHandshake = 22;
constexpr uint8_t kTlsHandshakeNewSessionTicket = 4;
constexpr uint8_t kTlsAlertCloseNotify = 0;
} // namespace

ControlAction classifyKtlsControlRecord(uint8_t record_type, const uint8_t* data, size_t len) {
  switch (record_type) {
  case kTlsRecordAlert:
    if (len >= 2 && data[1] == kTlsAlertCloseNotify) {
      return ControlAction::Eof;
    }
    return ControlAction::Close;
  case kTlsRecordHandshake: {
    // Tolerate NewSessionTicket and close on any unsupported post-handshake message.
    size_t pos = 0;
    while (pos < len) {
      if (data[pos] != kTlsHandshakeNewSessionTicket) {
        return ControlAction::Close;
      }
      // Split handshake headers need userspace TLS reassembly, so close instead.
      if (pos + 4 > len) {
        return ControlAction::Close;
      }
      const size_t msg_len = (static_cast<size_t>(data[pos + 1]) << 16) |
                             (static_cast<size_t>(data[pos + 2]) << 8) |
                             static_cast<size_t>(data[pos + 3]);
      if (msg_len > len - pos - 4) {
        return ControlAction::Close; // Declared length overruns the record.
      }
      pos += 4 + msg_len;
    }
    return ControlAction::Retry;
  }
  case kTlsRecordChangeCipherSpec:
    return ControlAction::Retry; // TLS 1.3 middlebox-compat record.
  default:
    return ControlAction::Close;
  }
}

bool spliceLegIsRawOrKtls(Network::Connection& connection) {
  if (connection.ssl() != nullptr) {
    return false;
  }
  OptRef<const Network::KtlsBytestreamInfo> info = connection.ktlsBytestreamInfo();
  if (!info.has_value()) {
    return true; // Plaintext raw socket.
  }
  return info->installed; // kTLS-capable socket.
}

#if defined(__linux__)

namespace {
// Pipe capacity. The kernel clamps to /proc/sys/fs/pipe-max-size.
constexpr size_t kPipeCapacity = 1024 * 1024;
// Floor used only when the kernel does not report pipe capacity.
constexpr size_t kFallbackPipeCapacity = 64 * 1024;
// Upper bound on non-DATA kTLS control records drained in one pump pass.
constexpr int kMaxControlRecordsPerPass = 1024;
// TLS 1.3 max record, 16384 plaintext plus AEAD and header overhead.
constexpr size_t kMaxTlsRecordSize = 16640;
} // namespace

SplicePump::SplicePump(os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls,
                       Event::Dispatcher& dispatcher, CompletionCb on_complete,
                       BytesCb on_upstream_to_downstream, BytesCb on_downstream_to_upstream)
    : down_fd_(down_fd), up_fd_(up_fd), up_is_ktls_(up_is_ktls), dispatcher_(dispatcher),
      on_complete_(std::move(on_complete)), on_u2d_bytes_(std::move(on_upstream_to_downstream)),
      on_d2u_bytes_(std::move(on_downstream_to_upstream)) {}

SplicePump::~SplicePump() {
  // Socket fds are borrowed and stay owned by their ConnectionImpls.
  for (int fd : {u2d_.read_fd, u2d_.write_fd, d2u_.read_fd, d2u_.write_fd}) {
    if (fd >= 0) {
      ::close(fd);
    }
  }
}

bool SplicePump::createPipes(bool need_u2d, bool need_d2u) {
  // Bounded HTTP body splice uses one direction per engage.
  if (need_u2d && u2d_.read_fd < 0) {
    int u2d[2];
    if (::pipe2(u2d, O_NONBLOCK | O_CLOEXEC) != 0) {
      ENVOY_LOG(warn, "splice pump u2d pipe2 failed, {}", std::strerror(errno));
      return false;
    }
    u2d_.read_fd = u2d[0];
    u2d_.write_fd = u2d[1];
  }
  if (need_d2u && d2u_.read_fd < 0) {
    int d2u[2];
    if (::pipe2(d2u, O_NONBLOCK | O_CLOEXEC) != 0) {
      ENVOY_LOG(warn, "splice pump d2u pipe2 failed, {}", std::strerror(errno));
      return false;
    }
    d2u_.read_fd = d2u[0];
    d2u_.write_fd = d2u[1];
  }
  return true;
}

bool SplicePump::prepare(std::string initial_u2d, std::string initial_d2u) {
  // Lazily create both pipes for callers that did not pre-create them.
  if (u2d_.read_fd < 0 && d2u_.read_fd < 0) {
    if (!createPipes(/*need_u2d=*/true, /*need_d2u=*/true)) {
      return false;
    }
  }

  // Write the pre-engage upstream chunk before any spliced upstream-to-downstream bytes.
  if (!initial_u2d.empty()) {
    size_t off = 0;
    while (off < initial_u2d.size()) {
      const ssize_t w = ::write(down_fd_, initial_u2d.data() + off, initial_u2d.size() - off);
      if (w > 0) {
        off += static_cast<size_t>(w);
        on_u2d_bytes_(static_cast<uint64_t>(w));
      } else if (w < 0 && errno == EINTR) {
        continue;
      } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break; // Socket full, stash the rest below.
      } else {
        ENVOY_LOG(warn, "splice pump initial downstream write error, {}", std::strerror(errno));
        if (off == 0) {
          return false; // Nothing delivered yet.
        }
        break; // Some bytes already delivered.
      }
    }
    if (off < initial_u2d.size()) {
      pending_down_ = std::move(initial_u2d);
      pending_down_off_ = off;
    }
  }

  // Write the pre-engage downstream chunk before any spliced downstream-to-upstream bytes.
  if (!initial_d2u.empty()) {
    size_t off = 0;
    while (off < initial_d2u.size()) {
      const ssize_t w = ::write(up_fd_, initial_d2u.data() + off, initial_d2u.size() - off);
      if (w > 0) {
        off += static_cast<size_t>(w);
        on_d2u_bytes_(static_cast<uint64_t>(w));
      } else if (w < 0 && errno == EINTR) {
        continue;
      } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        break; // Socket full, stash the rest below.
      } else {
        ENVOY_LOG(warn, "splice pump initial upstream write error, {}", std::strerror(errno));
        if (off == 0) {
          return false; // Nothing delivered yet.
        }
        break;
      }
    }
    if (off < initial_d2u.size()) {
      pending_up_ = std::move(initial_d2u);
      pending_up_off_ = off;
    }
  }
  return true;
}

void SplicePump::setBounds(absl::optional<uint64_t> u2d_limit, absl::optional<uint64_t> d2u_limit) {
  bounded_ = true;
  u2d_limit_ = u2d_limit;
  d2u_limit_ = d2u_limit;
}

void SplicePump::arm() {
  // Trust the kernel-reported pipe capacity, not the requested size.
  auto size_pipe = [](Pipe& pipe) {
    if (pipe.write_fd < 0) {
      return;
    }
    const int set = ::fcntl(pipe.write_fd, F_SETPIPE_SZ, static_cast<int>(kPipeCapacity));
    if (set > 0) {
      pipe.capacity = static_cast<size_t>(set);
      return;
    }
    const int got = ::fcntl(pipe.write_fd, F_GETPIPE_SZ);
    pipe.capacity = got > 0 ? static_cast<size_t>(got) : kFallbackPipeCapacity;
  };
  size_pipe(u2d_);
  size_pipe(d2u_);

  up_file_event_ = dispatcher_.createFileEvent(
      up_fd_, [this](uint32_t events) { return onUpReady(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed);
  down_file_event_ = dispatcher_.createFileEvent(
      down_fd_, [this](uint32_t events) { return onDownReady(events); },
      Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed);

  // Assume ready at arm and let EAGAIN clear the latches.
  up_readable_ = up_writable_ = down_readable_ = down_writable_ = true;
  ENVOY_LOG(debug, "splice pump armed down_fd={} up_fd={} kTLS={} pending={}", down_fd_, up_fd_,
            up_is_ktls_, pending_down_.size() - pending_down_off_);
  pump();
}

absl::Status SplicePump::onUpReady(uint32_t events) {
  if (completed_) {
    return absl::OkStatus();
  }
  if (events & Event::FileReadyType::Closed) {
    up_closed_ = true;
  }
  if (events & (Event::FileReadyType::Read | Event::FileReadyType::Closed)) {
    up_readable_ = true;
  }
  if (events & Event::FileReadyType::Write) {
    up_writable_ = true;
  }
  pump();
  return absl::OkStatus();
}

absl::Status SplicePump::onDownReady(uint32_t events) {
  if (completed_) {
    return absl::OkStatus();
  }
  if (events & Event::FileReadyType::Closed) {
    down_closed_ = true;
  }
  if (events & (Event::FileReadyType::Read | Event::FileReadyType::Closed)) {
    down_readable_ = true;
  }
  if (events & Event::FileReadyType::Write) {
    down_writable_ = true;
  }
  pump();
  return absl::OkStatus();
}

void SplicePump::pump() {
  if (completed_) {
    return;
  }
  // Reset the authoritative upstream drain marker each pass.
  up_eagain_this_pass_ = false;
  // Re-test write readiness each pass. Edge-triggered EPOLLOUT may not fire again after a
  // transient EAGAIN if the socket never left writable state.
  up_writable_ = true;
  down_writable_ = true;
  // SPLICE_F_MORE is set only while more bytes are expected in bounded mode.
  const unsigned base_flags = SPLICE_F_NONBLOCK | SPLICE_F_MOVE;
  int control_records = 0;
  bool progress = true;
  while (progress) {
    progress = false;

    // Unbounded mode has no message boundary, so it never sets SPLICE_F_MORE.
    const bool u2d_more_expected =
        bounded_ && u2d_limit_.has_value() && u2d_read_ < u2d_limit_.value();
    const bool d2u_more_expected =
        bounded_ && d2u_limit_.has_value() && d2u_read_ < d2u_limit_.value();
    const unsigned u2d_out_flags = base_flags | (u2d_more_expected ? SPLICE_F_MORE : 0u);
    const unsigned d2u_out_flags = base_flags | (d2u_more_expected ? SPLICE_F_MORE : 0u);

    // Flush pre-engage upstream bytes before spliced upstream-to-downstream bytes.
    while (down_writable_ && pending_down_off_ < pending_down_.size()) {
      const ssize_t w = ::write(down_fd_, pending_down_.data() + pending_down_off_,
                                pending_down_.size() - pending_down_off_);
      if (w > 0) {
        pending_down_off_ += static_cast<size_t>(w);
        on_u2d_bytes_(static_cast<uint64_t>(w));
        progress = true;
      } else if (w == 0) {
        complete(SpliceCompletion::Closed);
        return;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        down_writable_ = false;
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        complete(SpliceCompletion::Closed);
        return;
      }
    }

    // Drain upstream-to-downstream pipe after the pre-engage chunk.
    while (down_writable_ && u2d_.in_pipe > 0 && pending_down_off_ >= pending_down_.size()) {
      const ssize_t n =
          ::splice(u2d_.read_fd, nullptr, down_fd_, nullptr, u2d_.in_pipe, u2d_out_flags);
      if (n > 0) {
        u2d_.in_pipe -= static_cast<size_t>(n);
        on_u2d_bytes_(static_cast<uint64_t>(n));
        progress = true;
      } else if (n == 0) {
        complete(SpliceCompletion::Closed);
        return;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        down_writable_ = false;
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        ENVOY_LOG(debug, "splice pump u2d downstream write error, {}", std::strerror(errno));
        complete(SpliceCompletion::Closed);
        return;
      }
    }

    // Fill upstream-to-downstream pipe without reading past its byte budget.
    while (up_readable_ && !up_read_eof_ && u2d_.in_pipe < u2d_.capacity &&
           (!bounded_ || (u2d_limit_.has_value() && u2d_read_ < u2d_limit_.value()))) {
      size_t want = u2d_.capacity - u2d_.in_pipe;
      if (bounded_) {
        want = std::min(want, static_cast<size_t>(u2d_limit_.value() - u2d_read_));
      }
      const ssize_t n = ::splice(up_fd_, nullptr, u2d_.write_fd, nullptr, want, base_flags);
      if (n > 0) {
        u2d_.in_pipe += static_cast<size_t>(n);
        u2d_read_ += static_cast<uint64_t>(n);
        progress = true;
      } else if (n == 0) {
        up_read_eof_ = true;
        break;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Source EAGAIN proves a drained socket only when the pipe is empty. A full pipe can also
        // return EAGAIN and must not be promoted to EOF.
        if (u2d_.in_pipe == 0) {
          up_readable_ = false;
          up_eagain_this_pass_ = true;
          // Closed plus drained read side is the TCP half-close signal.
          if (up_closed_) {
            up_read_eof_ = true;
          }
        }
        break;
      } else if (errno == EINTR) {
        continue;
      } else if (errno == EINVAL && up_is_ktls_) {
        if (drainUpstreamControlMessage()) {
          if (++control_records > kMaxControlRecordsPerPass) {
            ENVOY_LOG(debug, "splice pump too many kTLS control records, closing");
            complete(SpliceCompletion::Closed);
            return;
          }
          progress = true;
          continue; // Benign control record consumed.
        }
        if (completed_) {
          return; // Fatal record or error closed the pump.
        }
        break; // Stop reading upstream but keep draining downstream-to-upstream bytes.
      } else {
        ENVOY_LOG(debug, "splice pump u2d upstream read error, {}", std::strerror(errno));
        complete(SpliceCompletion::Closed);
        return;
      }
    }

    // Flush pre-engage downstream bytes before spliced downstream-to-upstream bytes.
    while (up_writable_ && pending_up_off_ < pending_up_.size()) {
      const ssize_t w = ::write(up_fd_, pending_up_.data() + pending_up_off_,
                                pending_up_.size() - pending_up_off_);
      if (w > 0) {
        pending_up_off_ += static_cast<size_t>(w);
        on_d2u_bytes_(static_cast<uint64_t>(w));
        progress = true;
      } else if (w == 0) {
        complete(SpliceCompletion::Closed);
        return;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        up_writable_ = false;
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        complete(SpliceCompletion::Closed);
        return;
      }
    }

    // Drain downstream-to-upstream pipe after the pre-engage chunk.
    while (up_writable_ && d2u_.in_pipe > 0 && pending_up_off_ >= pending_up_.size()) {
      const ssize_t n =
          ::splice(d2u_.read_fd, nullptr, up_fd_, nullptr, d2u_.in_pipe, d2u_out_flags);
      if (n > 0) {
        d2u_.in_pipe -= static_cast<size_t>(n);
        on_d2u_bytes_(static_cast<uint64_t>(n));
        progress = true;
      } else if (n == 0) {
        complete(SpliceCompletion::Closed);
        return;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        up_writable_ = false;
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        ENVOY_LOG(debug, "splice pump d2u upstream write error, {}", std::strerror(errno));
        complete(SpliceCompletion::Closed);
        return;
      }
    }

    // Fill downstream-to-upstream pipe without reading past its byte budget.
    while (down_readable_ && !down_read_eof_ && d2u_.in_pipe < d2u_.capacity &&
           (!bounded_ || (d2u_limit_.has_value() && d2u_read_ < d2u_limit_.value()))) {
      size_t want = d2u_.capacity - d2u_.in_pipe;
      if (bounded_) {
        want = std::min(want, static_cast<size_t>(d2u_limit_.value() - d2u_read_));
      }
      const ssize_t n = ::splice(down_fd_, nullptr, d2u_.write_fd, nullptr, want, base_flags);
      if (n > 0) {
        d2u_.in_pipe += static_cast<size_t>(n);
        d2u_read_ += static_cast<uint64_t>(n);
        progress = true;
      } else if (n == 0) {
        down_read_eof_ = true;
        // Re-check upstream before completing after a downstream EOF.
        if (!up_read_eof_ && !up_readable_) {
          up_readable_ = true;
          progress = true;
        }
        break;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Source EAGAIN proves a drained socket only when the pipe is empty. A full pipe can also
        // return EAGAIN and must not be promoted to EOF.
        if (d2u_.in_pipe == 0) {
          down_readable_ = false;
          // Closed plus drained read side is the TCP half-close signal.
          if (down_closed_ && !down_read_eof_) {
            down_read_eof_ = true;
            if (!up_read_eof_ && !up_readable_) {
              up_readable_ = true;
              progress = true;
            }
          }
        }
        break;
      } else if (errno == EINTR) {
        continue;
      } else {
        ENVOY_LOG(debug, "splice pump d2u downstream read error, {}", std::strerror(errno));
        complete(SpliceCompletion::Closed);
        return;
      }
    }
  }
  maybeHalfCloseOrComplete();
}

bool SplicePump::drainUpstreamControlMessage() {
  // Non-DATA TLS records must be consumed through recvmsg().
  uint8_t buf[kMaxTlsRecordSize];
  alignas(struct cmsghdr) char cmsg_space[CMSG_SPACE(sizeof(uint8_t))];
  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = sizeof(buf);
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
      up_eagain_this_pass_ = true; // Authoritative upstream drain.
      return false;
    }
    ENVOY_LOG(debug, "splice pump kTLS control recvmsg error, {}", std::strerror(errno));
    complete(SpliceCompletion::Closed);
    return false;
  }
  if (n == 0) {
    // Upstream closed its read side.
    up_read_eof_ = true;
    maybeHalfCloseOrComplete();
    return false;
  }
  // Oversized or truncated records cannot be classified safely.
  if (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) {
    ENVOY_LOG(debug, "splice pump kTLS control record truncated");
    complete(SpliceCompletion::Closed);
    return false;
  }

  uint8_t record_type = 0;
  for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_TLS && cmsg->cmsg_type == TLS_GET_RECORD_TYPE &&
        cmsg->cmsg_len >= CMSG_LEN(sizeof(uint8_t))) {
      record_type = *reinterpret_cast<uint8_t*>(CMSG_DATA(cmsg));
    }
  }

  switch (classifyKtlsControlRecord(record_type, buf, static_cast<size_t>(n))) {
  case ControlAction::Retry:
    return true;
  case ControlAction::Eof:
    ENVOY_LOG(debug, "splice pump upstream close_notify");
    up_read_eof_ = true;
    maybeHalfCloseOrComplete();
    return false;
  case ControlAction::Close:
    if (record_type == kTlsRecordAlert) {
      ENVOY_LOG(debug, "splice pump upstream fatal TLS alert level {} desc {}", buf[0],
                n >= 2 ? buf[1] : 0xFF);
    } else {
      ENVOY_LOG(debug, "splice pump closing on TLS record type {}", record_type);
    }
    complete(SpliceCompletion::Closed);
    return false;
  }
  return false; // All ControlActions are handled above.
}

bool SplicePump::upstreamHasExtraneousData() {
  // A successful peek means DATA is queued past Content-Length.
  uint8_t b;
  ssize_t n;
  do {
    n = ::recv(up_fd_, &b, 1, MSG_PEEK | MSG_DONTWAIT);
  } while (n < 0 && errno == EINTR);
  return n > 0;
}

void SplicePump::sendUpstreamCloseNotify() {
  // Best-effort close_notify for strict kTLS peers.
  uint8_t alert[2] = {1, kTlsAlertCloseNotify}; // Warning level and close_notify.
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
  if (cmsg == nullptr) {
    return;
  }
  cmsg->cmsg_level = SOL_TLS;
  cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
  cmsg->cmsg_len = CMSG_LEN(sizeof(uint8_t));
  *CMSG_DATA(cmsg) = kTlsRecordAlert;
  ssize_t rc;
  do {
    rc = ::sendmsg(up_fd_, &msg, MSG_DONTWAIT);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) {
    ENVOY_LOG(debug, "splice pump close_notify sendmsg failed, {}", std::strerror(errno));
  }
}

void SplicePump::maybeHalfCloseOrComplete() {
  if (completed_) {
    return;
  }
  if (bounded_) {
    // Bounded mode never half-closes.
    if ((u2d_limit_.has_value() && up_read_eof_ && u2d_read_ < u2d_limit_.value()) ||
        (d2u_limit_.has_value() && down_read_eof_ && d2u_read_ < d2u_limit_.value())) {
      complete(SpliceCompletion::Closed);
      return;
    }
    // A bounded direction is done once its budget and pending bytes are flushed.
    const bool u2d_done =
        !u2d_limit_.has_value() || (u2d_read_ >= u2d_limit_.value() && u2d_.in_pipe == 0 &&
                                    pending_down_off_ >= pending_down_.size());
    const bool d2u_done =
        !d2u_limit_.has_value() || (d2u_read_ >= d2u_limit_.value() && d2u_.in_pipe == 0 &&
                                    pending_up_off_ >= pending_up_.size());
    if (u2d_done && d2u_done) {
      // DATA past Content-Length would be smuggled onto the next pooled stream. kTLS control
      // records are benign and are drained by the next read.
      if (u2d_limit_.has_value() && upstreamHasExtraneousData()) {
        ENVOY_LOG(debug, "splice pump: extraneous upstream data past Content-Length, not reusable");
        complete(SpliceCompletion::Closed);
        return;
      }
      complete(SpliceCompletion::BoundsReached);
    }
    return;
  }
  const bool u2d_drained =
      up_read_eof_ && u2d_.in_pipe == 0 && pending_down_off_ >= pending_down_.size();
  const bool d2u_drained =
      down_read_eof_ && d2u_.in_pipe == 0 && pending_up_off_ >= pending_up_.size();
  // Half-close downstream write after the upstream side drains.
  if (u2d_drained && !down_write_shutdown_) {
    ::shutdown(down_fd_, SHUT_WR);
    down_write_shutdown_ = true;
  }
  if (d2u_drained && !up_write_shutdown_) {
    if (up_is_ktls_) {
      sendUpstreamCloseNotify();
    }
    ::shutdown(up_fd_, SHUT_WR);
    up_write_shutdown_ = true;
  }
  if (u2d_drained && d2u_drained) {
    complete(SpliceCompletion::Closed);
    return;
  }
  // Once upstream is done, complete after any upload bytes finish draining.
  if (u2d_drained && down_write_shutdown_ && d2u_.in_pipe == 0 &&
      pending_up_off_ >= pending_up_.size()) {
    complete(SpliceCompletion::Closed);
    return;
  }
  // Complete after client EOF only after this pass observes upstream EAGAIN. A stale readable latch
  // does not prove the RX buffer is empty.
  if (d2u_drained && up_write_shutdown_ && up_eagain_this_pass_ && u2d_.in_pipe == 0 &&
      pending_down_off_ >= pending_down_.size()) {
    complete(SpliceCompletion::Closed);
  }
}

void SplicePump::complete(SpliceCompletion status) {
  if (completed_) {
    return;
  }
  completed_ = true;
  ENVOY_LOG(debug, "splice pump complete");
  // Disable FileEvents before completion because we may be inside one of them.
  if (up_file_event_ != nullptr) {
    up_file_event_->setEnabled(0);
  }
  if (down_file_event_ != nullptr) {
    down_file_event_->setEnabled(0);
  }
  // The owner defers pump destruction through its own callback.
  on_complete_(status);
}

#else // !defined(__linux__)

// kTLS and splice() are Linux-only.
SplicePump::SplicePump(os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls,
                       Event::Dispatcher& dispatcher, CompletionCb on_complete,
                       BytesCb on_upstream_to_downstream, BytesCb on_downstream_to_upstream)
    : down_fd_(down_fd), up_fd_(up_fd), up_is_ktls_(up_is_ktls), dispatcher_(dispatcher),
      on_complete_(std::move(on_complete)), on_u2d_bytes_(std::move(on_upstream_to_downstream)),
      on_d2u_bytes_(std::move(on_downstream_to_upstream)) {}

SplicePump::~SplicePump() = default;
bool SplicePump::createPipes(bool, bool) { return false; }
bool SplicePump::prepare(std::string, std::string) { return false; }
void SplicePump::setBounds(absl::optional<uint64_t>, absl::optional<uint64_t>) {}
void SplicePump::arm() {}
absl::Status SplicePump::onUpReady(uint32_t) { return absl::OkStatus(); }
absl::Status SplicePump::onDownReady(uint32_t) { return absl::OkStatus(); }
void SplicePump::pump() {}
bool SplicePump::drainUpstreamControlMessage() { return false; }
bool SplicePump::upstreamHasExtraneousData() { return false; }
void SplicePump::sendUpstreamCloseNotify() {}
void SplicePump::maybeHalfCloseOrComplete() {}
void SplicePump::complete(SpliceCompletion) {}

#endif

} // namespace TcpProxy
} // namespace Envoy
