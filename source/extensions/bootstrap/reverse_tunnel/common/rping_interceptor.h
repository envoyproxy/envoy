#pragma once

#include "source/common/network/io_socket_handle_impl.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class RpingInterceptor : public virtual Network::IoSocketHandleImpl {
public:
  // Strip RPING keepalives off the socket. The raw transport reads via read(); the TLS transport
  // reads via readv() (the BoringSSL BIO). read() dispatches through the virtual readv(), so the
  // processing_read_ guard keeps the two paths mutually exclusive and each RPING processed once.
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               std::optional<uint64_t> max_length) override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;

  virtual void onPingMessage() PURE;

protected:
  // Whether to actively echo RPING messages while the connection is idle.
  // Disabled permanently after the first non-RPING application byte is observed.
  bool ping_echo_active_{true};

private:
  // Drains a complete RPING from `buffer` (echoing via onPingMessage()) and returns the result
  // with the RPING hidden from the caller. Backs the read() path.
  Api::IoCallUint64Result applyRpingToBuffer(Buffer::Instance& buffer,
                                             Api::IoCallUint64Result result);

  // Copies `src` across `slices`, returning bytes written. Caller guarantees src fits.
  uint64_t scatterToSlices(absl::string_view src, Buffer::RawSlice* slices, uint64_t num_slice);

  // Set while read() drives the base read()->readv() dispatch; makes readv() a pure passthrough.
  bool processing_read_{false};

  // Partial RPING prefix carried across readv() calls until the keepalive completes; <=4 bytes.
  absl::InlinedVector<char, 5> partial_ping_;
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
