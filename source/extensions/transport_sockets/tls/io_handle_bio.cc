#include "extensions/transport_sockets/tls/io_handle_bio.h"

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
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      BIO_set_retry_read(b);
    }
    return -1;
  }
  return result.rc_;
}

// NOLINTNEXTLINE(readability-identifier-naming)
int io_handle_write(BIO* b, const char* in, int inl) {
  Envoy::Buffer::RawSlice slice;
  slice.mem_ = const_cast<char*>(in);
  slice.len_ = inl;
  auto result = bio_io_handle(b)->writev(&slice, 1);
  BIO_clear_retry_flags(b);
  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      BIO_set_retry_write(b);
    }
    return -1;
  }
  return result.rc_;
}

// NOLINTNEXTLINE(readability-identifier-naming)
long io_handle_ctrl(BIO* b, int cmd, long num, void* ptr) {
  long ret = 1;

  switch (cmd) {
  case BIO_C_SET_FD:
    io_handle_free(b);
    b->num = -1;
    b->ptr = ptr;
    b->shutdown = int(num);
    b->init = 1;
    break;
  case BIO_C_GET_FD:
    ret = -1;
    *reinterpret_cast<void**>(ptr) = b->ptr;
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

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
const BIO_METHOD* BIO_s_io_handle(void) { return &methods_io_handlep; }

// NOLINTNEXTLINE(readability-identifier-naming)
BIO* BIO_new_io_handle(Envoy::Network::IoHandle* io_handle) {
  BIO* ret;

  ret = BIO_new(BIO_s_io_handle());
  RELEASE_ASSERT(ret != nullptr, "");
  BIO_ctrl(ret, BIO_C_SET_FD, 0, io_handle);
  return ret;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy