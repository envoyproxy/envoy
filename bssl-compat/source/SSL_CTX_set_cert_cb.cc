#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"


namespace {

// This holds the caller's actual callback and argument values
struct Wrapper {
  int (*callback)(SSL* ssl, void* arg);
  void* arg;
};

// Our callback wrapper function that checks if SSL is a client or server. If
// it's a client, just call through to the user's actual callback. Otherwise, if
// SSL is a server, log an error message and return 0 to abort the handshake.
extern "C" int wrapper_cb(SSL* ssl, void* arg) {
  Wrapper* wrapper = static_cast<Wrapper*>(arg);

  if (SSL_is_server(ssl)) {
    bssl_compat_error("Using SSL_CTX_set_cert_cb() on a server is unsuported");
    return 0;
  }

  return wrapper->callback(ssl, wrapper->arg);
}

}


extern "C" void
SSL_CTX_set_cert_cb(SSL_CTX* ctx,
                    int (*callback)(SSL *ssl, void* arg), void* arg) {
  // Allocate an index used for storing the Wrapper in the SSL_CTX.
  // The free_func prevents the wrapper leaking when the SSL_CTX is deleted.
  static const int index{SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr,
      +[](void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
        if (ptr) {
          delete static_cast<Wrapper*>(ptr);
        }
      })};

  // Delete the previous callback if there's one set
  if (void *prev = SSL_CTX_get_ex_data(ctx, index)) {
    delete static_cast<Wrapper*>(prev);
  }

  if (callback == nullptr) { // null callback case
    ossl.ossl_SSL_CTX_set_ex_data(ctx, index, nullptr);
    ossl.ossl_SSL_CTX_set_cert_cb(ctx, nullptr, nullptr);
    return;
  }

  // Allocate a new wrapper
  std::unique_ptr<Wrapper> wrapper {std::make_unique<Wrapper>(callback, arg)};

  // Transfer wrapper ownership to the slot and set the callback
  ossl.ossl_SSL_CTX_set_ex_data(ctx, index, wrapper.get());
  ossl.ossl_SSL_CTX_set_cert_cb(ctx, wrapper_cb, wrapper.release());
}
