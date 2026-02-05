#include "source/common/tls/io_handle_bio.h"

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"

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

#ifdef ENVOY_SSL_OPENSSL
// OpenSSL requires explicit create/destroy methods for BIO
// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_new(BIO* bio) {
  BIO_set_init(bio, 0);
  BIO_set_data(bio, nullptr);
  BIO_clear_flags(bio, ~0);
  return 1;
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_free(BIO* bio) {
  if (bio == nullptr) {
    return 0;
  }

  if (BIO_get_shutdown(bio)) {
    if (BIO_get_init(bio)) {
      bio_io_handle(bio)->close();
    }
    BIO_set_init(bio, 0);
    BIO_clear_flags(bio, ~0);
  }
  return 1;
}
#endif

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_read(BIO* b, char* out, int outl) {
  if (out == nullptr) {
    return 0;
  }

  Envoy::Buffer::RawSlice slice;
  slice.mem_ = out;
  slice.len_ = outl;
  auto result = bio_io_handle(b)->readv(outl, &slice, 1);
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
#ifdef ENVOY_SSL_OPENSSL
  // Track EOF so that BIO_eof() returns the correct value. OpenSSL 3.2+ record layer
  // calls BIO_eof() to distinguish EOF from errors when BIO_read returns 0.
  if (result.return_value_ == 0) {
    BIO_set_flags(b, 0x800); // BIO_FLAGS_IN_EOF
  }
#endif
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
long io_handle_ctrl(BIO* b, int cmd, long num, void*) {
  long ret = 1;

#ifndef ENVOY_SSL_OPENSSL
  // Suppress unused parameter warnings when building with BoringSSL
  (void)b;
  (void)num;
#endif

  switch (cmd) {
#ifdef ENVOY_SSL_OPENSSL
  // OpenSSL-specific BIO control commands
  case BIO_C_SET_FD:
    RELEASE_ASSERT(false, "should not be called");
    break;
  case BIO_C_GET_FD:
    RELEASE_ASSERT(false, "should not be called");
    break;
  case BIO_CTRL_GET_CLOSE:
    ret = BIO_get_shutdown(b);
    break;
  case BIO_CTRL_SET_CLOSE:
    BIO_set_shutdown(b, int(num));
    break;
  case BIO_CTRL_EOF:
    ret = (BIO_test_flags(b, 0x800) != 0); // BIO_FLAGS_IN_EOF
    break;
#endif
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
#ifdef ENVOY_SSL_OPENSSL
    // OpenSSL requires explicit create/destroy methods
    RELEASE_ASSERT(BIO_meth_set_create(ret, io_handle_new), "");
    RELEASE_ASSERT(BIO_meth_set_destroy(ret, io_handle_free), "");
#endif
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
#ifdef ENVOY_SSL_OPENSSL
  BIO_set_shutdown(b, 0);
#endif
  BIO_set_init(b, 1);

  return b;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
