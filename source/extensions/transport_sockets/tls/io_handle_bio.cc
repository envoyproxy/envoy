#include "source/extensions/transport_sockets/tls/io_handle_bio.h"

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
  return reinterpret_cast<Envoy::Network::IoHandle*>(bio->ptr);
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_new(BIO* bio) {
  bio->init = 0;
  bio->num = -1;
  bio->ptr = nullptr;
  bio->flags = 0;
  return 1;
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_free(BIO* bio) {
  if (bio == nullptr) {
    return 0;
  }

  if (bio->shutdown) {
    if (bio->init) {
      bio_io_handle(bio)->close();
    }
    bio->init = 0;
    bio->flags = 0;
  }
  return 1;
}

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

  switch (cmd) {
  case BIO_C_SET_FD:
    RELEASE_ASSERT(false, "should not be called");
    break;
  case BIO_C_GET_FD:
    RELEASE_ASSERT(false, "should not be called");
    break;
  case BIO_CTRL_GET_CLOSE:
    ret = b->shutdown;
    break;
  case BIO_CTRL_SET_CLOSE:
    b->shutdown = int(num);
    break;
  case BIO_CTRL_FLUSH:
    ret = 1;
    break;
  default:
    ret = 0;
    break;
  }
  return ret;
}

const BIO_METHOD methods_io_handlep = {
    BIO_TYPE_SOCKET,    "io_handle",
    io_handle_write,    io_handle_read,
    nullptr /* puts */, nullptr /* gets, */,
    io_handle_ctrl,     io_handle_new,
    io_handle_free,     nullptr /* callback_ctrl */,
};

// NOLINTNEXTLINE(readability-identifier-naming)
const BIO_METHOD* BIO_s_io_handle(void) { return &methods_io_handlep; }

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BIO* BIO_new_io_handle(Envoy::Network::IoHandle* io_handle) {
  BIO* b;

  b = BIO_new(BIO_s_io_handle());
  RELEASE_ASSERT(b != nullptr, "");

  // Initialize the BIO
  b->num = -1;
  b->ptr = io_handle;
  b->shutdown = 0;
  b->init = 1;

  return b;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
