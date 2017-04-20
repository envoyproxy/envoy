#pragma once

#include <memory>
#include <mutex>

#include "common/common/c_smart_ptr.h"

#include "openssl/ssl.h"

namespace Ssl {

/**
 * Global functionality specific to OpenSSL.
 */
class OpenSsl final {
public:
  /**
   * Initialize the library globally.
   */
  static void initialize();

private:
#ifndef OPENSSL_IS_BORINGSSL
  static unsigned long getThreadIdCb() { return pthread_self(); }
  static void threadLockCb(int mode, int which, const char* file, int line);

  static std::unique_ptr<std::mutex[]> locks_;
#endif
};

typedef CSmartPtr<X509, X509_free> X509Ptr;

} // Ssl
