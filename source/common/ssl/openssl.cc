#include "openssl.h"

#include "common/common/assert.h"

#include "openssl/rand.h"
#include "openssl/ssl.h"

namespace Ssl {

#ifndef OPENSSL_IS_BORINGSSL
std::unique_ptr<std::mutex[]> OpenSsl::locks_;
#endif

void OpenSsl::initialize() {
  SSL_load_error_strings();

  RELEASE_ASSERT(SSL_library_init());
  RELEASE_ASSERT(RAND_poll());
  OpenSSL_add_all_algorithms();

#ifndef OPENSSL_IS_BORINGSSL
  locks_.reset(new std::mutex[CRYPTO_num_locks()]);
  CRYPTO_set_id_callback(getThreadIdCb);
  CRYPTO_set_locking_callback(threadLockCb);
#endif
}

#ifndef OPENSSL_IS_BORINGSSL

void OpenSsl::threadLockCb(int mode, int which, const char*, int) {
  if (mode & CRYPTO_LOCK) {
    locks_[which].lock();
  } else {
    locks_[which].unlock();
  }
}

#endif

} // Ssl
