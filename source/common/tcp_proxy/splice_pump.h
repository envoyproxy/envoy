#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace TcpProxy {

// What to do with a non-DATA kTLS record drained from the upstream socket.
enum class ControlAction {
  Retry, // benign record consumed (NewSessionTicket or ChangeCipherSpec), retry the splice.
  Eof,   // peer sent close_notify, treat the upstream read as EOF.
  Close, // fatal alert or unsupported record, tear the splice down.
};

// Terminal outcome reported once through the SplicePump completion callback.
enum class SpliceCompletion {
  // A bounded transfer moved exactly the configured byte budget in every active direction. The
  // borrowed sockets are intact and can carry the next keep-alive message, so the caller resumes
  // the codecs rather than closing.
  BoundsReached,
  // The transfer ended because a peer closed or half-closed, an unbounded pump ran a socket to EOF,
  // or an error occurred. The sockets must not be reused.
  Closed,
};

// Classifies a non-DATA TLS record by its record type and bytes. Pure so it can be unit-tested
// without a kTLS socket. `data` and `len` are the decrypted record payload from recvmsg(). Exposed
// here for unit testing.
ControlAction classifyKtlsControlRecord(uint8_t record_type, const uint8_t* data, size_t len);

// True if raw spliced bytes can be sent to or read from the connection without bypassing
// encryption. Plaintext and installed-kTLS legs pass. Userspace TLS and kTLS-capable legs without
// installed kTLS fail.
bool spliceLegIsRawOrKtls(Network::Connection& connection);

// In-kernel splice() pump that moves bytes through pipes while borrowing both socket fds. The
// caller owns the sockets. The pump owns only its pipes and FileEvents. Each edge-triggered wakeup
// drains ready directions to EAGAIN without blocking.
class SplicePump : public Logger::Loggable<Logger::Id::filter> {
public:
  using CompletionCb = std::function<void(SpliceCompletion)>;
  using BytesCb = std::function<void(uint64_t)>;

  SplicePump(os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls, Event::Dispatcher& dispatcher,
             CompletionCb on_complete, BytesCb on_upstream_to_downstream,
             BytesCb on_downstream_to_upstream);
  // Virtual for deterministic coordinator tests. These methods are not on the byte path.
  virtual ~SplicePump();

  // Creates only the requested pipe directions before callers do irreversible work.
  virtual bool createPipes(bool need_u2d, bool need_d2u);
  // Queues pre-engage chunks before callers detach ConnectionImpl FileEvents. The chunks are moved
  // out of the caller's buffers (zero-copy), so they are left empty on return.
  virtual bool prepare(Buffer::Instance& initial_u2d, Buffer::Instance& initial_d2u);
  // Switches the pump into bounded mode for exact Content-Length bodies.
  virtual void setBounds(absl::optional<uint64_t> u2d_limit, absl::optional<uint64_t> d2u_limit);
  // Installs the pump's FileEvents after callers detach ConnectionImpl FileEvents.
  virtual void arm();

private:
  struct Pipe {
    int read_fd{-1};
    int write_fd{-1};
    size_t in_pipe{0};
    size_t capacity{0};
  };

  absl::Status onDownReady(uint32_t events);
  absl::Status onUpReady(uint32_t events);
  void pump();
  bool drainUpstreamControlMessage();
  // Checks if DATA is queued past a bounded download's Content-Length boundary.
  bool upstreamHasExtraneousData();
  void sendUpstreamCloseNotify();
  void maybeHalfCloseOrComplete();
  void complete(SpliceCompletion status);

  // The state below is only used by the Linux splice()/kTLS fast path; the non-Linux build compiles
  // stub methods that never touch it. Guarding it keeps that build free of unused-private-field
  // errors.
#if defined(__linux__)
  const os_fd_t down_fd_;
  const os_fd_t up_fd_;
  const bool up_is_ktls_;
  Event::Dispatcher& dispatcher_;
  CompletionCb on_complete_;
  BytesCb on_u2d_bytes_;
  BytesCb on_d2u_bytes_;

  // Bounded-mode state for exact Content-Length bodies.
  bool bounded_{false};
  absl::optional<uint64_t> u2d_limit_;
  absl::optional<uint64_t> d2u_limit_;
  uint64_t u2d_read_{0};
  uint64_t d2u_read_{0};

  Pipe u2d_; // Upstream to downstream.
  // Downstream to upstream.
  Pipe d2u_;
  Event::FileEventPtr up_file_event_;
  Event::FileEventPtr down_file_event_;

  // Pre-engage upstream chunk waiting for downstream socket space.
  Buffer::OwnedImpl pending_down_;
  // Pre-engage downstream chunk waiting for upstream socket space.
  Buffer::OwnedImpl pending_up_;

  // Readiness latches set by FileEvents and cleared on EAGAIN.
  bool up_readable_{false};
  bool up_writable_{false};
  bool down_readable_{false};
  bool down_writable_{false};
  bool up_read_eof_{false};
  bool down_read_eof_{false};
  bool down_write_shutdown_{false};
  bool up_write_shutdown_{false};
  bool completed_{false};
  // True only when this pump pass proved the upstream RX buffer is drained.
  bool up_eagain_this_pass_{false};
  // Set when the FileEvent reports Closed.
  bool down_closed_{false};
  bool up_closed_{false};
#endif
};

using SplicePumpPtr = std::unique_ptr<SplicePump>;

} // namespace TcpProxy
} // namespace Envoy
