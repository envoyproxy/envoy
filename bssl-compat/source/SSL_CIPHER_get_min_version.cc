/*
 * Copyright (C) 2022 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <openssl/ssl.h>
#include <ossl.h>


/*
 * BoringSSL only returns: TLS1_3_VERSION, TLS1_2_VERSION, or SSL3_VERSION
 */
extern "C" uint16_t SSL_CIPHER_get_min_version(const SSL_CIPHER *cipher) {
  // This logic was copied from BoringSSL's ssl_cipher.cc

  if ((ossl.ossl_SSL_CIPHER_get_kx_nid(cipher) == ossl_NID_kx_any) ||
      (ossl.ossl_SSL_CIPHER_get_auth_nid(cipher) == ossl_NID_auth_any)) {
    return TLS1_3_VERSION;
  }

  const EVP_MD *digest = ossl.ossl_SSL_CIPHER_get_handshake_digest(cipher);
  if ((digest == nullptr) || (ossl.ossl_EVP_MD_get_type(digest) != NID_md5_sha1)) {
    return TLS1_2_VERSION;
  }

  return SSL3_VERSION;
}
