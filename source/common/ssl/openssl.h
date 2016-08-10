#pragma once

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
  static unsigned long getThreadIdCb() { return pthread_self(); }
  static void threadLockCb(int mode, int which, const char* file, int line);

  static std::unique_ptr<std::mutex[]> locks_;
};

typedef CSmartPtr<X509, X509_free> X509Ptr;

} // Ssl
