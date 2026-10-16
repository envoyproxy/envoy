#include "source/common/tls/io_handle_bio.h"

#include <algorithm>
#include <limits>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/network/io_handle.h"

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

  uint64_t pending() const { return end_ - begin_; }

  Network::IoHandle& io_handle_;
  std::unique_ptr<char[]> read_ahead_;
  uint32_t read_ahead_size_{0};
  uint64_t begin_{0};
  uint64_t end_{0};
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
    const uint64_t size = std::min<uint64_t>(outl, state.pending());
    std::copy_n(state.read_ahead_.get() + state.begin_, size, out);
    state.begin_ += size;
    return size;
  }

  if (state.read_ahead_size_ > 0 && !state.read_ahead_) {
    state.read_ahead_ = std::make_unique<char[]>(state.read_ahead_size_);
  }

  Envoy::Buffer::RawSlice slice;
  slice.mem_ = state.read_ahead_ ? state.read_ahead_.get() : out;
  // Some socket APIs, including Winsock recv(), take a signed int length.
  slice.len_ = state.read_ahead_
                   ? std::min<uint64_t>(state.read_ahead_size_, std::numeric_limits<int>::max())
                   : static_cast<uint64_t>(outl);
  auto* io_handle = bio_io_handle(b);
  auto result = io_handle->readv(slice.len_, &slice, 1);
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
  if (state.read_ahead_ && result.return_value_ > 0) {
    ASSERT(result.return_value_ <= state.read_ahead_size_);
    const uint64_t size = std::min<uint64_t>(outl, result.return_value_);
    std::copy_n(state.read_ahead_.get(), size, out);
    state.begin_ = size;
    state.end_ = result.return_value_;
    return size;
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

bool enableIoHandleBioReadAhead(BIO* bio, uint32_t size) {
  if (bio == nullptr || BIO_method_type(bio) != ioHandleBioType()) {
    return false;
  }
  auto& state = bioState(bio);
  ASSERT(state.read_ahead_size_ == 0 || state.read_ahead_size_ == size);
  if (state.read_ahead_size_ == 0) {
    state.read_ahead_size_ = size;
  }
  return true;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
