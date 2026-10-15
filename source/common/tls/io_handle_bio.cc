#include "source/common/tls/io_handle_bio.h"

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

// NOLINTNEXTLINE(readability-identifier-naming)
inline Envoy::Network::IoHandle* bio_io_handle(BIO* bio) {
  return reinterpret_cast<Envoy::Network::IoHandle*>(BIO_get_data(bio));
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_read(BIO* b, char* out, int outl) {
  if (out == nullptr) {
    return 0;
  }

  Envoy::Buffer::RawSlice slice;
  slice.mem_ = out;
  slice.len_ = outl;
  auto* io_handle = bio_io_handle(b);
  auto result = io_handle->readv(outl, &slice, 1);
  BIO_clear_retry_flags(b);
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
long io_handle_ctrl(BIO*, int cmd, long, void*) {
  long ret = 1;

  switch (cmd) {
  case BIO_CTRL_FLUSH:
    ret = 1;
    break;
  default:
    ret = 0;
    break;
  }
  return ret;
}

// NOLINTNEXTLINE(readability-identifier-naming)
const BIO_METHOD* BIO_s_io_handle(void) {
  static const BIO_METHOD* method = [&] {
    BIO_METHOD* ret = BIO_meth_new(BIO_TYPE_SOCKET, "io_handle");
    RELEASE_ASSERT(ret != nullptr, "");
    RELEASE_ASSERT(BIO_meth_set_read(ret, io_handle_read), "");
    RELEASE_ASSERT(BIO_meth_set_write(ret, io_handle_write), "");
    RELEASE_ASSERT(BIO_meth_set_ctrl(ret, io_handle_ctrl), "");
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
  BIO_set_data(b, io_handle);
  BIO_set_init(b, 1);

  return b;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
