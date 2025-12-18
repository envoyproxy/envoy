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
#include "override.h"


/*
 * BoringSSL
 * =========
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L1586
 * 
 * SSL_get_peer_full_cert_chain returns the peer's certificate chain, or NULL
 * if unavailable or the peer did not use certificates. This is the unverified
 * list of certificates as sent by the peer, not the final chain built during
 * verification. The caller does not take ownership of the result.
 * 
 * This is the same as |SSL_get_peer_cert_chain| except that this function
 * always returns the full chain, i.e. the first element of the return value
 * (if any) will be the leaf certificate. In constrast,
 * |SSL_get_peer_cert_chain| returns only the intermediate certificates if the
 * |ssl| is a server.
 * 
 * OpenSSL
 * =======
 * 
 * SSL_get_peer_cert_chain() returns a pointer to STACK_OF(X509) certificates
 * forming the certificate chain sent by the peer. If called on the client side,
 * the stack also contains the peer's certificate; if called on the server side,
 * the peer's certificate must be obtained separately using
 * SSL_get_peer_certificate(3). If the peer did not present a certificate, NULL
 * is returned.
 * 
 * SSL_get0_peer_certificate() & SSL_get1_peer_certificate() return a pointer to
 * the X509 certificate the peer presented. If the peer did not present a
 * certificate, NULL is returned.
 * 
 * SSL_get1_peer_certificate() The reference count of the X509 object returned
 * is incremented by one, so that it will not be destroyed when the session
 * containing the peer certificate is freed. The X509 object must be explicitly
 * freed using X509_free().
 * 
 * SSL_get0_peer_certificate() The reference count of the X509 object returned
 * is not incremented, and must not be freed.
 */
extern "C" STACK_OF(X509) *SSL_get_peer_full_cert_chain(const SSL *ssl) {
  // If someone has provided us with an override result, just return that
  if (auto r = OverrideResult<SSL_get_peer_full_cert_chain>::get(ssl)) {
    return r->value();
  }

  // For a client, SSL_get_peer_cert_chain() gives us exactly what we want in
  // terms of the content and ownership semantics so just return the result.
  if (!ossl_SSL_is_server(ssl)) {
    return reinterpret_cast<STACK_OF(X509)*>(ossl_SSL_get_peer_cert_chain(ssl));
  }

  // For a server we have to concatenate the client's leaf + intermediate
  // certificates (if any), and we also need to return the correct ownership
  if (X509* leaf = ossl_SSL_get0_peer_certificate(ssl)) {
    STACK_OF(X509) *result {nullptr};

    if (STACK_OF(X509) *chain = reinterpret_cast<STACK_OF(X509)*>(ossl_SSL_get_peer_cert_chain(ssl))) {
      result = sk_X509_dup(chain);
      sk_X509_insert(result, leaf, 0);
    }
    else {
      result = sk_X509_new_null();
      sk_X509_push(result, leaf);
    }

    // Now squirrel away the STACK_OF(X509) so that we can return it without
    // returning ownership to the caller, also ensuring that it doesn't leak
    static int index {SSL_get_ex_new_index(0, nullptr, nullptr, nullptr,
                          +[](void *, void *ptr, CRYPTO_EX_DATA *, int, long, void*) {
                            if (ptr) sk_X509_free(reinterpret_cast<STACK_OF(X509)*>(ptr));
                          })};
    if (void *old = ossl_SSL_get_ex_data(const_cast<SSL*>(ssl), index)) {
      sk_X509_free(reinterpret_cast<STACK_OF(X509)*>(old));
    }
    ossl_SSL_set_ex_data(const_cast<SSL*>(ssl), index, result);

    return result;
  }

  return nullptr;
}
