#include "openssl.h"

#include "common/common/assert.h"

#include "openssl/rand.h"
#include "openssl/ssl.h"

namespace Ssl {

std::unique_ptr<std::mutex[]> OpenSsl::locks_;

void OpenSsl::initialize() {
  SSL_load_error_strings();

  if (!SSL_library_init()) {
    PANIC("OpenSSL initialization failed");
  }

  // Seed PRNG from /dev/urandom
  if (!RAND_poll()) {
    PANIC("could not seed random number generator")
  }

  locks_.reset(new std::mutex[CRYPTO_num_locks()]);

  OpenSSL_add_all_algorithms();
  CRYPTO_set_id_callback(getThreadIdCb);
  CRYPTO_set_locking_callback(threadLockCb);
}

void OpenSsl::threadLockCb(int mode, int which, const char*, int) {
  if (mode & CRYPTO_LOCK) {
    locks_[which].lock();
  } else {
    locks_[which].unlock();
  }
}

} // Ssl
