#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/common/logger.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {

// What to do with a non-data kTLS record read from the upstream socket.
enum class ControlAction {
  Retry, // Benign record consumed (NewSessionTicket or ChangeCipherSpec), retry the read.
  Eof,   // Peer sent close_notify, treat the upstream read as EOF.
  Close, // Fatal alert or unsupported record, tear the splice down.
};

// Classifies a non-data TLS record by its type and payload. Pure so it can be unit tested without a
// kTLS socket. data and len are the decrypted record bytes from recvmsg.
ControlAction classifyKtlsControlRecord(uint8_t record_type, const uint8_t* data, size_t len);

// Engine-run splice pump for the dynamic-module transport socket fast path. It relays a plaintext
// downstream socket and a plaintext or kTLS upstream socket. The upload splices downstream bytes
// through a pipe into the upstream socket, and a kTLS TX socket encrypts them in-kernel with no
// user space copy. A kTLS download is read with recvmsg and written straight to the downstream
// socket, because splice from a kTLS RX socket can leave the final record undelivered with no
// further read edge. A plaintext download is spliced through a second pipe. The socket fds are
// borrowed and stay owned by their connections, the pump owns only its pipes and file events. Each
// end of stream is forwarded as a half-close, and the pump invokes on_complete once both
// directions end or an error occurs.
class SplicePump : public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  // How the session ended. Done is a graceful end of both directions. On an error the caller resets
  // both connections, starting with the one that did not fail, so its peer sees the truncation
  // rather than a clean close.
  enum class Outcome { Done, DownstreamError, UpstreamError };
  // Invoked once when the session ends.
  using CompletionCb = std::function<void(Outcome outcome)>;
  // Bytes one wakeup may deliver before the pump yields the worker to other connections.
  static constexpr uint64_t DefaultMaxBytesPerWakeup = 2 * 1024 * 1024;

  SplicePump(os_fd_t down_fd, os_fd_t up_fd, Event::Dispatcher& dispatcher,
             CompletionCb on_complete, uint64_t max_bytes_per_wakeup = DefaultMaxBytesPerWakeup);
  ~SplicePump();

  // Checks that both sockets are non-blocking, the downstream is plaintext, and the upstream has
  // kTLS in both directions or, unless upstream_needs_ktls is set, none at all. It then creates the
  // pipes it needs, and returns false on any failure so the caller can stay on the buffered path.
  bool prepare(bool upstream_needs_ktls);
  // Sizes the pipes, installs the pump file events, and runs the first pass. Call after prepare
  // and after the caller has removed the connections' own file events.
  void arm();

  // Plaintext bytes delivered to the upstream and to the downstream so far.
  uint64_t bytesUploaded() const { return bytes_uploaded_; }
  uint64_t bytesDownloaded() const { return bytes_downloaded_; }

private:
  struct Pipe {
    int read_fd{-1};
    int write_fd{-1};
    size_t in_pipe{0};
    size_t capacity{0};
  };

  // Outcome of one kTLS read.
  enum class ReadResult {
    Data,    // Application data was stored in download_buf_.
    Control, // A benign control record was consumed, keep reading.
    Stop,    // The socket is empty, at EOF, or the pump completed.
  };

  absl::Status onUpReady(uint32_t events);
  absl::Status onDownReady(uint32_t events);
  void pump();
  // Reads decrypted data from the kTLS RX socket with recvmsg. Application data is stored in
  // download_buf_ for the pump to write to the downstream socket, and control records are
  // classified.
  ReadResult readUpstreamRecord();
  void maybeComplete();
  void complete(Outcome outcome);
  // Returns true once the alert is sent. Returns false when a full kTLS TX buffer refused it, so
  // the caller retries on the next write edge, or when it failed and the pump completed with an
  // upstream error.
  bool sendUpstreamCloseNotify();

  const os_fd_t down_fd_;
  const os_fd_t up_fd_;
  Event::Dispatcher& dispatcher_;
  CompletionCb on_complete_;
  const uint64_t max_bytes_per_wakeup_;
  // Whether prepare found kTLS on the upstream socket.
  bool up_is_ktls_{false};

  Pipe u2d_; // Upstream to downstream, the plaintext download. Unused for a kTLS download.
  Pipe d2u_; // Downstream to upstream, the upload.
  // The kTLS download read with recvmsg, how many bytes it holds, and how many have reached the
  // downstream socket. A short write keeps the rest for the next pass.
  std::vector<uint8_t> download_buf_;
  size_t download_len_{0};
  size_t download_offset_{0};
  Event::FileEventPtr up_file_event_;
  Event::FileEventPtr down_file_event_;
  // Resumes a pass that stopped at its byte budget, on the next loop iteration.
  Event::SchedulableCallbackPtr resume_cb_;
  uint64_t bytes_uploaded_{0};
  uint64_t bytes_downloaded_{0};

  // Read readiness latches set by the file events and cleared on EAGAIN.
  bool up_readable_{false};
  bool down_readable_{false};
  bool up_read_eof_{false};
  bool down_read_eof_{false};
  bool down_write_shutdown_{false};
  bool up_write_shutdown_{false};
  bool completed_{false};
};

using SplicePumpPtr = std::unique_ptr<SplicePump>;

} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
