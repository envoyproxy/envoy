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
#include "log.h"


extern "C" void SSL_set_renegotiate_mode(SSL *ssl, enum ssl_renegotiate_mode_t mode) {
  switch(mode) {
    case ssl_renegotiate_never: {
      ossl.ossl_SSL_clear_options(ssl,ossl_SSL_OP_ALLOW_CLIENT_RENEGOTIATION);
      break;
    }
    case ssl_renegotiate_freely: {
      ossl.ossl_SSL_set_options(ssl,ossl_SSL_OP_ALLOW_CLIENT_RENEGOTIATION);
      break;
    }
    case ssl_renegotiate_once: {
      bssl_compat_fatal("%s(ssl_renegotiate_once) NYI", __func__);
      break;
    }
    case ssl_renegotiate_ignore: {
      bssl_compat_fatal("%s(ssl_renegotiate_ignore) NYI", __func__);
      break;
    }
    case ssl_renegotiate_explicit: {
      bssl_compat_fatal("%s(ssl_renegotiate_explicit) NYI", __func__);
      break;
    }
  }
}
