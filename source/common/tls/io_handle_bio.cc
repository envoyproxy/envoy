#include "source/common/tls/io_handle_bio.h"

#include <algorithm>
#include <limits>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/network/io_handle.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "openssl/bio.h"
#include "openssl/err.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

int ioHandleBioType() {
  static const int type = [] {
    const int index = BIO_get_new_index();
    RELEASE_ASSERT(index != -1, "Failed to allocate IoHandle BIO type");
    return index | BIO_TYPE_SOURCE_SINK;
  }();
  return type;
}

struct IoHandleBioState {
  explicit IoHandleBioState(Network::IoHandle& io_handle) : io_handle_(io_handle) {}

  uint64_t pending() const { return read_ahead_.length(); }

  uint64_t copyOut(char* out, uint64_t length) {
    const uint64_t size = std::min(length, pending());
    read_ahead_.copyOut(0, size, out);
    read_ahead_.drain(size);
    return size;
  }

  Api::IoCallUint64Result read(char* out, uint64_t length) {
    if (read_ahead_size_ == 0) {
      Buffer::RawSlice slice{out, static_cast<size_t>(length)};
      return io_handle_.readv(length, &slice, 1);
    }

    Buffer::Reservation reservation = read_ahead_.reserveForRead();
    const uint64_t max_length = std::min(read_ahead_size_, reservation.length());
    auto result = io_handle_.readv(max_length, reservation.slices(), reservation.numSlices());
    reservation.commit(result.ok() ? result.return_value_ : 0);
    return result;
  }

  Network::IoHandle& io_handle_;
  Buffer::OwnedImpl read_ahead_;
  uint64_t read_ahead_size_{0};
};

IoHandleBioState& bioState(BIO* bio) { return *static_cast<IoHandleBioState*>(BIO_get_data(bio)); }

// NOLINTNEXTLINE(readability-identifier-naming)
inline Envoy::Network::IoHandle* bio_io_handle(BIO* bio) { return &bioState(bio).io_handle_; }

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_read(BIO* b, char* out, int outl) {
  if (out == nullptr || outl <= 0) {
    return 0;
  }

  auto& state = bioState(b);
  BIO_clear_retry_flags(b);
  if (state.pending() > 0) {
    return state.copyOut(out, outl);
  }

  auto* io_handle = bio_io_handle(b);
  auto result = state.read(out, outl);
  if (!result.ok()) {
    auto err = result.err_->getErrorCode();
    if (err == Api::IoError::IoErrorCode::Again || err == Api::IoError::IoErrorCode::Interrupt) {
      BIO_set_retry_read(b);
    } else {
      ERR_put_error(ERR_LIB_SYS, 0, result.err_->getSystemErrorCode(), __FILE__, __LINE__);
    }
    return -1;
  }
  // On EOF (readv returned 0 successfully), check SO_ERROR for a pending TCP RST.
  // Some peers tear the connection down with RST instead of FIN; readv returns 0
  // in that case but a connection error is pending on the socket. Push it onto
  // the error queue so the SSL layer can detect it via drainErrorQueue().
  if (result.return_value_ == 0 &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.ssl_socket_report_connection_reset")) {
    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    auto opt_result = io_handle->getOption(SOL_SOCKET, SO_ERROR, &so_error, &so_error_len);
    if (opt_result.return_value_ == 0 && so_error != 0) {
      ERR_put_error(ERR_LIB_SYS, 0, so_error, __FILE__, __LINE__);
    }
  }
  if (state.pending() > 0) {
    return state.copyOut(out, outl);
  }
  return result.return_value_;
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_write(BIO* b, const char* in, int inl) {
  Envoy::Buffer::RawSlice slice;
  slice.mem_ = const_cast<char*>(in);
  slice.len_ = inl;
  auto result = bio_io_handle(b)->writev(&slice, 1);
  BIO_clear_retry_flags(b);
  if (!result.ok()) {
    auto err = result.err_->getErrorCode();
    if (err == Api::IoError::IoErrorCode::Again || err == Api::IoError::IoErrorCode::Interrupt) {
      BIO_set_retry_write(b);
    } else {
      ERR_put_error(ERR_LIB_SYS, 0, result.err_->getSystemErrorCode(), __FILE__, __LINE__);
    }
    return -1;
  }
  return result.return_value_;
}

// NOLINTNEXTLINE(readability-identifier-naming)
long io_handle_ctrl(BIO* bio, int cmd, long, void*) {
  long ret = 1;

  switch (cmd) {
  case BIO_CTRL_FLUSH:
    ret = 1;
    break;
  case BIO_CTRL_PENDING:
    ret = std::min<uint64_t>(bioState(bio).pending(), std::numeric_limits<long>::max());
    break;
  default:
    ret = 0;
    break;
  }
  return ret;
}

int destroyIoHandleBio(BIO* bio) {
  delete static_cast<IoHandleBioState*>(BIO_get_data(bio));
  BIO_set_data(bio, nullptr);
  return 1;
}

// NOLINTNEXTLINE(readability-identifier-naming)
const BIO_METHOD* BIO_s_io_handle(void) {
  static const BIO_METHOD* method = [&] {
    BIO_METHOD* ret = BIO_meth_new(ioHandleBioType(), "io_handle");
    RELEASE_ASSERT(ret != nullptr, "");
    RELEASE_ASSERT(BIO_meth_set_read(ret, io_handle_read), "");
    RELEASE_ASSERT(BIO_meth_set_write(ret, io_handle_write), "");
    RELEASE_ASSERT(BIO_meth_set_ctrl(ret, io_handle_ctrl), "");
    RELEASE_ASSERT(BIO_meth_set_destroy(ret, destroyIoHandleBio), "");
    return ret;
  }();
  return method;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BIO* BIO_new_io_handle(Envoy::Network::IoHandle* io_handle) {
  BIO* b;

  b = BIO_new(BIO_s_io_handle());
  RELEASE_ASSERT(b != nullptr, "");

  // Initialize the BIO
  BIO_set_data(b, new IoHandleBioState(*io_handle));
  BIO_set_init(b, 1);

  return b;
}

bool setIoHandleBioReadAheadSize(BIO* bio, uint64_t size) {
  if (bio == nullptr || BIO_method_type(bio) != ioHandleBioType()) {
    return false;
  }
  auto& state = bioState(bio);
  // Some socket APIs, including Winsock recv(), take a signed int length.
  state.read_ahead_size_ = std::min<uint64_t>(size, std::numeric_limits<int>::max());
  return true;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
