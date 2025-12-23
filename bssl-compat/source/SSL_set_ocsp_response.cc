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

#include "SSL_CTX_set_select_certificate_cb.h"

typedef std::pair<std::unique_ptr<void, decltype(&OPENSSL_free)>, size_t> OcspResponse;

static int index() {
  static int index{ossl.ossl_SSL_get_ex_new_index(
      0, nullptr, nullptr, nullptr, +[](void*, void* ptr, CRYPTO_EX_DATA*, int, long, void*) {
        if (OcspResponse* resp = reinterpret_cast<OcspResponse*>(ptr)) {
          delete resp;
        }
      })};
  return index;
}

/**
 * This callback gets installed via SSL_CTX_set_tlsext_status_cb(...) in order to deal
 * with the deferred OCSP response that may have been set via SSL_set_ocsp_response()
 */
static int ssl_apply_deferred_ocsp_response_cb(SSL* ssl, void* arg) {
  std::unique_ptr<OcspResponse> resp{
      reinterpret_cast<OcspResponse*>(ossl.ossl_SSL_get_ex_data(ssl, index()))};

  if (resp) {
    ossl.ossl_SSL_set_ex_data(ssl, index(), nullptr);
    if (ossl.ossl_SSL_set_tlsext_status_ocsp_resp(ssl, resp->first.get(), resp->second) == 1) {
      resp->first.release(); // ossl_SSL_set_tlsext_status_ocsp_resp() took ownership
      return ossl_SSL_TLSEXT_ERR_OK;
    }
    return ossl_SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  return ossl_SSL_TLSEXT_ERR_NOACK;
}

/**
 * If this is called from within the select certificate callback, then we don't call
 * ossl_SSL_CTX_set_tlsext_status_cb() directly because it doesn't work from within that
 * callback. Instead, we squirel away the OCSP response bytes to be applied later on via
 * ossl_SSL_CTX_set_tlsext_status_cb() later on.
 */
extern "C" int SSL_set_ocsp_response(SSL* ssl, const uint8_t* response, size_t response_len) {
  std::unique_ptr<void, decltype(&OPENSSL_free)> response_copy(
      ossl.ossl_OPENSSL_memdup(response, response_len), OPENSSL_free);

  if (response_copy) {
    if (in_select_certificate_cb(ssl)) {

      SSL_CTX* ctx{ossl.ossl_SSL_get_SSL_CTX(ssl)};
      int (*callback)(SSL*, void*){nullptr};

      // Check that we are not overwriting another existing callback
      if (ossl_SSL_CTX_get_tlsext_status_cb(ctx, &callback) == 0) {
        return 0;
      }
      if (callback && (callback != ssl_apply_deferred_ocsp_response_cb)) {
        return 0;
      }

      // Install our callback to call the real SSL_set_ex_data() function later
      if (ossl_SSL_CTX_set_tlsext_status_cb(ctx, ssl_apply_deferred_ocsp_response_cb) == 0) {
        return 0;
      }

      // If we have been called previously, from within the same select
      // certificate callback invocation, there will be an OcspReponse object
      // squirreled away already. If so, delete it first, so we don't just
      // overwrite it and create a leak.
      if (OcspResponse* prev =
              reinterpret_cast<OcspResponse*>(ossl.ossl_SSL_get_ex_data(ssl, index()))) {
        delete prev;
      }

      // Store the OcspResponse bytes for the callback to pick up later
      std::unique_ptr<OcspResponse> resp =
          std::make_unique<OcspResponse>(std::move(response_copy), response_len);
      if (ossl.ossl_SSL_set_ex_data(ssl, index(), resp.get()) == 1) {
        resp.release(); // ossl_SSL_set_ex_data() took ownership
        return 1;
      }
    } else { // We're not in a select certificate callback, so we set it directly
      if (ossl.ossl_SSL_set_tlsext_status_ocsp_resp(ssl, response_copy.get(), response_len) == 1) {
        response_copy.release(); // ossl_SSL_set_tlsext_status_ocsp_resp() took ownership
        return 1;
      }
    }
  }

  return response ? 0 : 1;
}
