#include <openssl/ec_key.h>
#include <openssl/bytestring.h>
#include <ossl.h>
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/8bbefbfeee609b17622deedd100163c12f5c95dc/include/openssl/ec_key.h#L225
 * https://www.openssl.org/docs/man3.0/man3/d2i_ECPrivateKey.html
 */
extern "C" EC_KEY *EC_KEY_parse_private_key(CBS *cbs, const EC_GROUP *group) {
  if (group != nullptr) {
    bssl_compat_fatal("%s() with non-null group not implemented", __func__);
  }

  auto data1 {CBS_data(cbs)}; // save so we can work out the skip
  auto data2 {data1}; // gets advanced by d2i_ECPrivateKey()
  auto length {CBS_len(cbs)};

  EC_KEY *key = ossl.ossl_d2i_ECPrivateKey(nullptr, &data2, length);

  if (key) {
    if (!CBS_skip(cbs, data2 - data1)) {
      ossl.ossl_EC_KEY_free(key);
      key = nullptr;
    }
  }

  return key;
}
