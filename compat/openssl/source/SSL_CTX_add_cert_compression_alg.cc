#include <openssl/ssl.h>
#include <ossl.h>
#include <vector>


// OpenSSL offers a very different certificate compression API to BoringSSL...
//
// In BoringSSL, the user must pass the compression type id, and associated
// compress/decompress functions to the SSL_CTX_add_cert_compression_alg() API.
// In the case of a server, the order in which this API is called also
// determines the server's preference order.
//
// On OpenSSL, the compress/decompress functions are built in (depending on
// OpenSSL build options). This means that we simply ignore the
// compress/decompress function pointers and just track the algorithm IDs
// to pass to SSL_CTX_set1_cert_comp_preference() & SSL_CTX_compress_certs().
extern "C" int SSL_CTX_add_cert_compression_alg(SSL_CTX *ctx, uint16_t alg_id,
                  ssl_cert_compression_func_t, ssl_cert_decompression_func_t)
{
  // Use an ex data slot to accumulate the preference order in a vector
  static const int index = ossl.ossl_SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr,
      [](void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
        delete static_cast<std::vector<int>*>(ptr);
      });

  auto* algs = static_cast<std::vector<int>*>(ossl.ossl_SSL_CTX_get_ex_data(ctx, index));

  if (algs == nullptr) {
    algs = new std::vector<int>{alg_id};
    ossl.ossl_SSL_CTX_set_ex_data(ctx, index, algs);
  }
  else {
    for (auto id : *algs) {
      if (id == alg_id) {
        return 0;
      }
    }
    algs->push_back(alg_id);
  }

  // This (re)sets the algorithm preference order accumulated in the vector
  if (ossl.ossl_SSL_CTX_set1_cert_comp_preference(ctx, algs->data(), algs->size()) == 0) {
    return 0;
  }

  // This tells OpenSSL to actually perform compression using alg_id
  int count = ossl.ossl_SSL_CTX_compress_certs(ctx, alg_id);
  if (count == 0 && ossl.ossl_ERR_peek_error()) {
    return 0;
  }

  return 1;
}
